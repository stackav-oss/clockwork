# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""IR nodes for report groups and related components."""

from __future__ import annotations

import enum
import uuid
from dataclasses import dataclass, field
from decimal import Decimal
from typing import TYPE_CHECKING, TypeVar

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.cog import report_group_policy as rg_policy
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    cog_components,
    expr,
    interface,
    node,
    policy,
    primitive,
    representation,
    schema,
    schema_reg,
    typesys,
)
from clockwork.dsl.ir import signal as signal_module
from clockwork.dsl.ir.cst_util import format_line_with_error, get_span

if TYPE_CHECKING:
    from collections.abc import Iterable

    from fltk.fegen.pyrt.terminalsrc import Span


class ReportingStrategy(enum.Enum):
    """Enum for reporting strategy values matching the .clk definition."""

    BATCHED = "batched"
    POST_AGGREGATED = "post_aggregated"


class ReportGroupLogType(enum.Enum):
    """Enum for report group log type values matching the .clk definition."""

    NONE = "none"
    EVENT = "event"
    TELEMETRY = "telemetry"


EnumT = TypeVar("EnumT", ReportingStrategy, ReportGroupLogType)


@dataclass
class ReportGroupEntry(node.CstNode[cst.ReportGroupEntry], node.DocableEntity, node.NamedEntity):
    """A single entry in a report group - either a signal definition or reference."""

    signal: node.DeferredLookup[signal_module.Signal] | signal_module.Signal
    cog_private: bool
    instance_name: expr.Expr | str | None = None
    post_aggregation: set[signal_module.AggregationType] = field(default_factory=set)
    post_aggregation_text: str | None = None

    @classmethod
    def from_cst(
        cls: type[ReportGroupEntry],
        cst_node: cst.ReportGroupEntry,
        module: node.Module,
        parent_scope: node.Scope,
    ) -> ReportGroupEntry:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        if signal_cst := cst_node.maybe_cog_scope_signal():
            return cls._from_cog_scope_signal(signal_cst, module, parent_scope, cst_node)
        if signal_ref_cst := cst_node.maybe_signal_reference():
            return cls._from_signal_reference(signal_ref_cst, module, parent_scope, cst_node)

        msg = "ReportGroupEntry must be either a signal definition or signal reference"
        raise ValueError(msg)

    @classmethod
    def _from_cog_scope_signal(
        cls: type[ReportGroupEntry],
        signal_cst: cst.CogScopeSignal,
        module: node.Module,
        parent_scope: node.Scope,
        cst_node: cst.ReportGroupEntry,
    ) -> ReportGroupEntry:
        """Handle cog-scope signal definition."""
        signal_ir = signal_module.Signal.from_cst(signal_cst, module, parent_scope)
        return cls(
            scope=parent_scope,
            doc=signal_ir.doc,
            name=signal_ir.name,
            module=module,
            cst_node=cst_node,
            signal=signal_ir,
            cog_private=True,
            instance_name=None,
        )

    @classmethod
    def _from_signal_reference(
        cls: type[ReportGroupEntry],
        signal_ref_cst: cst.SignalReference,
        module: node.Module,
        parent_scope: node.Scope,
        cst_node: cst.ReportGroupEntry,
    ) -> ReportGroupEntry:
        """Handle signal reference: either 'identifier;' or 'local_name: signal_name;' with optional instance_name block."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        cst_doc = signal_ref_cst.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None

        identifiers = list(signal_ref_cst.children_identifier())
        if signal_name_cst := signal_ref_cst.maybe_signal_name():
            # Explicit form: local_name: signal_name;
            # First identifier is local_name, labeled identifier is signal_name
            identifier = get_span(identifiers[0].child_value(), module.terminals)
            signal_ir = node.DeferredLookup.make(
                expected_type=signal_module.Signal,
                cst_identifier=signal_name_cst,
                terminals=module.terminals,
            )
        else:
            # Simple form: identifier;
            # Single identifier is both local_name and signal_name
            identifier = get_span(identifiers[0].child_value(), module.terminals)
            signal_ir = node.DeferredLookup.make(
                expected_type=signal_module.Signal,
                cst_identifier=identifiers[0],
                terminals=module.terminals,
            )

        instance_name = None
        post_aggregation: set[signal_module.AggregationType] = set()
        post_aggregation_text: str | None = None
        if instance_block_cst := signal_ref_cst.maybe_signal_instance_block():
            instance_name, post_aggregation, post_aggregation_text = cls._parse_signal_instance_options(
                instance_block_cst.children_signal_instance_option(), module
            )
        return cls(
            scope=parent_scope,
            doc=doc,
            name=identifier,
            module=module,
            cst_node=cst_node,
            signal=signal_ir,
            cog_private=False,
            instance_name=instance_name,
            post_aggregation=post_aggregation,
            post_aggregation_text=post_aggregation_text,
        )

    @staticmethod
    def _parse_signal_instance_options(
        options: Iterable[cst.SignalInstanceOption], module: node.Module
    ) -> tuple[expr.Expr | None, set[signal_module.AggregationType], str | None]:
        """Parse instance_name and post_aggregation from signal instance options.

        Returns:
            A tuple of (instance_name, post_aggregation, post_aggregation_text).
        """
        instance_name = None
        post_aggregation: set[signal_module.AggregationType] = set()
        post_aggregation_text: str | None = None
        for option_cst in options:
            if instance_name_cst := option_cst.maybe_signal_instance_name():
                instance_name = expr.Expr.from_cst(instance_name_cst.child_value(), module)
            elif post_agg_cst := option_cst.maybe_signal_post_aggregation():
                post_aggregation, post_aggregation_text = signal_module.parse_aggregation(
                    post_agg_cst, module, "post-aggregation"
                )
        return instance_name, post_aggregation, post_aggregation_text

    def resolve(self, parent_scope: node.Scope, group_instance_name: str | None = None) -> None:
        """Perform finalization of the IR.

        Args:
            parent_scope: The parent scope to use for resolving signal references.
            group_instance_name: Instance name from the report group level, used as default if entry has no instance name.
        """
        if isinstance(self.signal, signal_module.Signal):
            self.signal.resolve()
        else:
            self.signal = self.signal.resolve(parent_scope)

        if isinstance(self.instance_name, expr.Expr):
            instance_name_value = self.instance_name.evaluate()
            if not isinstance(instance_name_value, primitive.StringValue):
                msg = self.instance_name.append_error_line(
                    f"Instance name must evaluate to a string, but got {instance_name_value!r}"
                )
                raise TypeError(msg)
            self.instance_name = instance_name_value.value
        elif self.instance_name is None and group_instance_name is not None:
            self.instance_name = group_instance_name

    def resolved_signal(self) -> signal_module.ResolvedSignal:
        """Get the resolved signal, raising an error if not yet resolved."""
        if not isinstance(self.signal, signal_module.Signal):
            msg = f"ReportGroupEntry signal '{self.name}' is not yet resolved."
            raise TypeError(msg)
        return self.signal.get_resolved()

    def resolved_instance_name(self) -> str | None:
        """Get the resolved instance name, raising an error if not yet resolved."""
        if isinstance(self.instance_name, expr.Expr):
            msg = f"ReportGroupEntry instance name for signal '{self.name}' is not yet resolved."
            raise TypeError(msg)
        return self.instance_name

    def effective_post_aggregation(self) -> set[signal_module.AggregationType]:
        """Get the effective post-aggregation for this entry.

        For cog-private signals (inline definitions), post_aggregation comes from the signal itself.
        For signal references, post_aggregation comes from the reference's instance block.

        Returns:
            The set of post-aggregation types to apply, or an empty set if none specified.
        """
        if self.cog_private:
            resolved = self.resolved_signal()
            return resolved.post_aggregation or set()
        return self.post_aggregation or set()

    def effective_post_aggregation_text(self) -> str:
        """Get the effective post-aggregation text for this entry.

        For cog-private signals (inline definitions), text comes from the signal itself.
        For signal references, text comes from the reference's instance block.

        Returns:
            The DSL text for post-aggregation, or empty string if none specified.
        """
        if self.cog_private:
            resolved = self.resolved_signal()
            return resolved.post_aggregation_text or ""
        return self.post_aggregation_text or ""


def _get_report_group_entry_identifier_span(entry_cst: cst.ReportGroupEntry) -> Span:
    """Get the identifier span from a report group entry for error reporting."""
    if signal_cst := entry_cst.maybe_cog_scope_signal():
        return signal_cst.child_identifier().child_value()
    if signal_ref_cst := entry_cst.maybe_signal_reference():
        # Get the first identifier (local_name in both simple and explicit forms)
        return next(signal_ref_cst.children_identifier()).child_value()
    return entry_cst.span


@dataclass
class ReportGroupDef(node.CstNode[cst.ReportGroup], node.DocableEntity, typesys.NamedAttribute):
    """A report group definition containing signal definitions and references."""

    entries: dict[str, ReportGroupEntry]
    report_group_config: ReportGroupConfig | None = None
    generated_schemas: list[schema.Schema] = field(default_factory=list)
    generated_interfaces: list[interface.InterfaceInstantiation] = field(default_factory=list)
    generated_representations: list[representation.ReprInstantiation] = field(default_factory=list)
    instance_name: expr.Expr | str | None = None
    generated_outer_schema: schema.InstantiatedSchema | None = None
    parent_cog_name: str | None = None
    log_type: cog_components.MetricsLogType = cog_components.MetricsLogType.none

    @classmethod
    def from_cst(
        cls: type[ReportGroupDef],
        cst_node: cst.ReportGroup,
        module: node.Module,
        parent_scope: node.Scope,
    ) -> ReportGroupDef:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        name = "default"
        if group_name_node := cst_node.maybe_group_name():
            name = get_span(group_name_node.child_value(), module.terminals)
            if name == "default":
                msg = format_line_with_error(
                    group_name_node.child_value(),
                    module.terminals,
                    module.module_id,
                )
                msg = f"Report group name cannot be 'default'{msg}"
                raise ValueError(msg)

        instance_name: expr.Expr | None = None
        if instance_name_node := cst_node.maybe_report_group_instance_name():
            instance_name = expr.Expr.from_cst(instance_name_node.child_value(), module)

        entries = {}
        for entry_cst in cst_node.children_report_group_entry():
            entry = ReportGroupEntry.from_cst(entry_cst, module, parent_scope)
            if entry.name in entries:
                identifier_span = _get_report_group_entry_identifier_span(entry_cst)
                msg = format_line_with_error(
                    identifier_span,
                    module.terminals,
                    module.module_id,
                )
                msg = f"Duplicate signal entry '{entry.name}' in report group{msg}"
                raise ValueError(msg)
            entries[entry.name] = entry

        result = cls(
            doc=None,
            name=name,
            scope=parent_scope,
            module=module,
            cst_node=cst_node,
            type_info=clkbuiltins.REPORT_GROUP_TYPE,
            entries=entries,
            instance_name=instance_name,
        )

        parent_scope.define(name, result, module.terminals)
        return result

    def resolve(self, parent_scope: node.Scope, parent_cog_name: str) -> None:
        """Perform finalization of the IR.

        Args:
            parent_scope: The parent scope to use for resolving signal references.
            parent_cog_name: The name of the parent cog, used to generate the schema name.
        """
        if self.generated_outer_schema is not None:
            return  # Already resolved

        self.parent_cog_name = parent_cog_name
        group_instance_name = self._resolve_instance_name()

        for entry in self.entries.values():
            entry.resolve(parent_scope, group_instance_name)

        self._load_report_group_config(parent_cog_name)
        self._set_log_type_from_config()
        self._validate_batched_no_post_aggregation()
        self._validate_post_aggregated_has_post_aggregation()
        self._generate_and_collect_schemas(parent_cog_name)

    def _resolve_instance_name(self) -> str | None:
        """Resolve the report group instance name.

        Returns:
            The resolved instance name as a string, or None if not specified.
        """
        if isinstance(self.instance_name, expr.Expr):
            instance_name_value = self.instance_name.evaluate()
            if not isinstance(instance_name_value, primitive.StringValue):
                msg = self.instance_name.append_error_line(
                    f"Instance name must evaluate to a string, but got {instance_name_value!r}"
                )
                raise TypeError(msg)
            group_instance_name = instance_name_value.value
            self.instance_name = group_instance_name
            return group_instance_name
        if isinstance(self.instance_name, str):
            return self.instance_name
        return None

    def _load_report_group_config(self, parent_cog_name: str) -> None:
        """Load the report group configuration from policy.

        Args:
            parent_cog_name: The name of the parent cog, used in error messages.

        Raises:
            ValueError: If no ReportGroupPolicy is found.
        """
        report_group_policy_class = rg_policy.get_report_group_policy(self.module.context)
        report_group_policy = policy.lookup_policy(self.module, report_group_policy_class, self)
        if report_group_policy is None:
            msg = self.append_error_line(
                f"No ReportGroupPolicy found for report group '{self.name}'. "
                + f"Define a policy with: policy ReportGroupPolicy for {parent_cog_name}.{self.name}"
            )
            raise ValueError(msg)
        self.report_group_config = ReportGroupConfig.from_policy_schema_instance(self, report_group_policy.data)

    def _set_log_type_from_config(self) -> None:
        """Set the log_type field based on the configured log type."""
        if self.report_group_config is None:
            return
        if self.report_group_config.log_type == ReportGroupLogType.TELEMETRY:
            self.log_type = cog_components.MetricsLogType.telemetry
        elif self.report_group_config.log_type == ReportGroupLogType.EVENT:
            self.log_type = cog_components.MetricsLogType.event
        else:
            self.log_type = cog_components.MetricsLogType.none

    def _generate_and_collect_schemas(self, parent_cog_name: str) -> None:
        """Generate schemas and collect them for the report group.

        Args:
            parent_cog_name: The name of the parent cog, used to generate the schema name.

        Raises:
            ValueError: If schema generation fails.
        """
        schema_instantiation, additional_schemas = self.generate_report_group_schema(parent_cog_name)
        if schema_instantiation is None:
            msg = self.append_error_line(
                f"Failed to generate schema for report group '{parent_cog_name}.{self.name}'. "
                + "Ensure the policy has a valid reporting_strategy."
            )
            raise ValueError(msg)

        self.generated_schemas = [s.schema.source for s in additional_schemas if s.schema.source is not None]
        self.generated_outer_schema = schema_instantiation
        if schema_instantiation.schema.source is not None:
            self.generated_schemas.append(schema_instantiation.schema.source)

        for additional_schema in additional_schemas:
            repr_inst, iface_inst = _get_repr_and_interface_from_schema(self.module, additional_schema)
            self.generated_representations.append(repr_inst)
            self.generated_interfaces.append(iface_inst)

        repr_inst, iface_inst = _get_repr_and_interface_from_schema(self.module, schema_instantiation)
        self.generated_representations.append(repr_inst)
        self.generated_interfaces.append(iface_inst)

    def _validate_batched_no_post_aggregation(self) -> None:
        """Validate that batched report groups don't have post-aggregation.

        Raises:
            ValueError: If a batched report group has entries with post-aggregation.
        """
        if self.report_group_config is None or (
            self.report_group_config.reporting_strategy != ReportingStrategy.BATCHED
        ):
            return
        for entry_name, entry in self.entries.items():
            post_agg = entry.effective_post_aggregation()
            if post_agg:
                post_agg_text = entry.effective_post_aggregation_text()
                msg = self.append_error_line(
                    "Batched report groups cannot specify post-aggregation. "
                    + f"Signal '{entry_name}' has post_aggregation: {post_agg_text}"
                )
                raise ValueError(msg)

    def _validate_post_aggregated_has_post_aggregation(self) -> None:
        """Validate that post-aggregated report groups have post-aggregation on all signals.

        Raises:
            ValueError: If a post-aggregated report group has entries without post-aggregation.
        """
        if self.report_group_config is None or (
            self.report_group_config.reporting_strategy != ReportingStrategy.POST_AGGREGATED
        ):
            return
        for entry_name, entry in self.entries.items():
            post_agg = entry.effective_post_aggregation()
            if not post_agg:
                msg = self.append_error_line(
                    f"Post-aggregated report groups require post_aggregation on all signals. Signal '{entry_name}' has no post_aggregation defined."
                )
                raise ValueError(msg)

    def generate_report_group_schema(
        self, parent_cog_name: str
    ) -> tuple[schema.InstantiatedSchema | None, list[schema.InstantiatedSchema]]:
        """Generate an instantiated schema representing the report group.

        Returns:
            A tuple of (main schema, additional schemas needed for the main schema).
        """
        if self.report_group_config is None:
            return None, []
        if self.report_group_config.reporting_strategy == ReportingStrategy.POST_AGGREGATED:
            return self._generate_post_aggregated_report_group_schema(parent_cog_name), []
        if self.report_group_config.reporting_strategy == ReportingStrategy.BATCHED:
            return self._generate_batched_report_group_schema(parent_cog_name)
        return None, []

    def make_instance(self, cog_instance_fqn: str) -> ReportGroupInstance:
        """Create an instantiated report group with resolved entries.

        Args:
            cog_instance_fqn: The fully qualified name of the cog containing this report group.

        Returns:
            An instantiated report group.
        """
        instantiated_entries: dict[str, ReportGroupEntryInstance] = {}
        for entry_name, entry in self.entries.items():
            resolved_signal = entry.resolved_signal()
            instance_name = entry.resolved_instance_name() if entry.resolved_instance_name() else cog_instance_fqn
            # invariant from above ternary operator
            assert instance_name

            entry_instance = ReportGroupEntryInstance(
                signal=resolved_signal,
                instance_name=instance_name,
                cog_private=entry.cog_private,
                post_aggregation=entry.effective_post_aggregation(),
            )

            instantiated_entries[entry_name] = entry_instance
        return ReportGroupInstance(group_def=self, entries=instantiated_entries, cog_instance_fqn=cog_instance_fqn)

    def get_generated_schema(self) -> schema.InstantiatedSchema:
        """Get the generated schema for this report group.

        Returns:
            The instantiated schema representing the report group.
        """
        if self.generated_outer_schema is None:
            msg = self.append_error_line(
                f"Report group '{self.name}' has no generated schema. Ensure resolve() has been called."
            )
            raise ValueError(msg)
        return self.generated_outer_schema

    def get_interface_info(self) -> schema_reg.InterfaceInfo:
        """Get the interface info for the generated report group schema.

        Returns:
            The interface info for the report group schema.
        """
        if not self.generated_outer_schema:
            msg = self.append_error_line(
                f"Report group '{self.name}' has no generated schemas. Ensure resolve() has been called."
            )
            raise ValueError(msg)
        return _get_interface_info_from_schema(self.module, self.generated_outer_schema)

    def _generate_post_aggregated_report_group_schema(self, parent_cog_name: str) -> schema.InstantiatedSchema:
        """Generate schema for post-aggregated report group.

        For post-aggregated report groups, each signal instance will have fields generated for each
        combination of its pre-aggregation types and the requested post-aggregation types.

        The schema will have the following structure:
        - execution_count: UInt16
        - execution_interval: Duration
        - {signal_name}_{pre_agg}_{post_agg}: signal_type (for each combination)
        - {signal_name}_{pre_agg}_{post_agg}_metadata: metadata_type (if metadata is preserved)

        Args:
            report_group_config: The report group policy configuration with aggregation settings.
            parent_cog_name: The name of the parent cog, used to generate the schema name.

        Returns:
            An instantiated schema representing the post-aggregated report group.
        """
        field_num_counter = 0
        fields: list[schema.FieldDef] = []

        # Add common fields
        fields.append(
            schema.make_field(
                self.module,
                field_num_counter,
                "execution_count",
                clkbuiltins.UINT16,
                doc="Number of cog executions represented in this message",
            )
        )
        field_num_counter += 1

        fields.append(
            schema.make_field(
                self.module,
                field_num_counter,
                "execution_interval",
                clkbuiltins.DURATION,
                doc="Duration over which signals were collected",
            )
        )
        field_num_counter += 1

        for entry_name, entry in self.entries.items():
            resolved_signal = entry.resolved_signal()

            post_agg_types = entry.effective_post_aggregation()
            pre_agg_types = resolved_signal.pre_aggregation

            signal_doc = resolved_signal.doc.value if resolved_signal.doc else None

            for pre_agg in pre_agg_types:
                for post_agg in post_agg_types:
                    field_type = self._get_field_type_for_aggregation(resolved_signal.signal_type, pre_agg, post_agg)

                    field_name = f"{entry_name}_{pre_agg.value}_{post_agg.value}"

                    field_doc = f"Aggregation {pre_agg.value} then {post_agg.value} of signal {entry_name}"
                    if signal_doc:
                        field_doc = f"{field_doc}: {signal_doc}"

                    fields.append(
                        schema.make_field(
                            self.module,
                            field_num_counter,
                            field_name,
                            field_type,
                            doc=field_doc,
                        )
                    )
                    field_num_counter += 1

                    if (
                        resolved_signal.metadata
                        and self._preserves_metadata(pre_agg)
                        and self._preserves_metadata(post_agg)
                    ):
                        metadata_field_name = f"{entry_name}_{pre_agg.value}_{post_agg.value}_metadata"
                        fields.append(
                            schema.make_field(
                                self.module,
                                field_num_counter,
                                metadata_field_name,
                                resolved_signal.metadata,
                                doc=f"Metadata for {field_name}",
                            )
                        )
                        field_num_counter += 1

        schema_name = f"{parent_cog_name}_{self.name}"
        return schema.make_schema_class(
            name=schema_name,
            module=self.module,
            fields=fields,
            doc=f"Post-aggregated report group schema for {self.name}",
            uuid=uuid.uuid3(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, schema_name),
        )

    def _generate_batched_report_group_schema(
        self, parent_cog_name: str
    ) -> tuple[schema.InstantiatedSchema, list[schema.InstantiatedSchema]]:
        """Generate schema for batched report group.

        For batched report groups, signals are stored in a VarSoa structure where each element
        represents a single cog execution's signal values.

        Two schemas are generated:
        - An inner SoA-enabled schema ({ReportGroupName}_Signal) containing signal fields
        - An outer schema ({CogName}_{ReportGroupName}) wrapping the VarSoa container

        Args:
            parent_cog_name: The name of the parent cog for generating the schema name.

        Returns:
            A tuple of (outer schema, [inner schema]).
        """
        assert self.report_group_config is not None
        assert self.report_group_config.max_observations is not None

        batch_size = self.report_group_config.max_observations

        inner_schema = self._generate_batched_inner_schema()

        var_soa_type = typesys.Instantiation(
            type_info=clkbuiltins.TYPE_TYPE,
            instantiates=clkbuiltins.VAR_SOA,
            arguments={
                "type": inner_schema,
                "max_size": primitive.DecimalValue(clkbuiltins.UINT64, Decimal(batch_size)),
            },
        )

        outer_fields: list[schema.FieldDef] = [
            schema.make_field(
                self.module,
                0,
                "execution_interval",
                clkbuiltins.DURATION,
                doc="Duration over which signals were collected",
            ),
            schema.make_field(
                self.module,
                1,
                "signals",
                var_soa_type,
                doc="SoA container holding batched signal values",
            ),
        ]

        outer_schema_name = f"{parent_cog_name}_{self.name}"
        outer_schema = schema.make_schema_class(
            name=outer_schema_name,
            module=self.module,
            fields=outer_fields,
            doc=f"Batched report group schema for {self.name}",
            uuid=uuid.uuid3(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, outer_schema_name),
        )
        return outer_schema, [inner_schema]

    def _generate_batched_inner_schema(self) -> schema.InstantiatedSchema:
        """Generate the inner SoA-enabled schema for batched report groups.

        The schema contains fields for each signal with its pre-aggregation type.
        For signals with metadata, a corresponding metadata field is added if the
        pre-aggregation preserves metadata.

        For batched report groups, MEAN pre-aggregation is expanded to SUM and COUNT,
        since mean can be computed from sum/count in post-processing.

        Returns:
            An instantiated SoA-enabled schema containing signal fields.
        """
        field_num_counter = 0
        fields: list[schema.FieldDef] = []

        for entry_name, entry in self.entries.items():
            resolved_signal = entry.resolved_signal()
            pre_agg_types = self._expand_mean_to_sum_count(resolved_signal.pre_aggregation)

            for pre_agg in pre_agg_types:
                field_name = f"{entry_name}_{pre_agg.value}"

                fields.append(
                    schema.make_field(
                        self.module,
                        field_num_counter,
                        field_name,
                        resolved_signal.signal_type,
                    )
                )
                field_num_counter += 1

                if resolved_signal.metadata and self._preserves_metadata(pre_agg):
                    metadata_field_name = f"{entry_name}_{pre_agg.value}_metadata"
                    fields.append(
                        schema.make_field(
                            self.module,
                            field_num_counter,
                            metadata_field_name,
                            resolved_signal.metadata,
                        )
                    )
                    field_num_counter += 1

        inner_schema_name = f"{self.name}_Signal"
        return schema.make_schema_class(
            name=inner_schema_name,
            module=self.module,
            fields=fields,
            options=schema.make_schema_options(self.module, soa_enabled=True, provide_constructor=True),
            doc=f"SoA element schema for batched report group {self.name}",
            uuid=uuid.uuid3(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, inner_schema_name),
        )

    def _get_field_type_for_aggregation(
        self,
        signal_type: typesys.TypeVal,
        pre_agg: signal_module.AggregationType,
        post_agg: signal_module.AggregationType,
    ) -> typesys.TypeVal:
        """Determine the field type for a given aggregation combination.

        For most aggregations, the type remains the same as the signal type.
        However, mean aggregation (either pre or post) results in Float32.

        Args:
            signal_type: The original signal type.
            pre_agg: The pre-aggregation type.
            post_agg: The post-aggregation type.

        Returns:
            The appropriate field type for the aggregation combination.
        """
        # Mean aggregation always results in Float32
        if pre_agg in {signal_module.AggregationType.MEAN} or post_agg in {signal_module.AggregationType.MEAN}:
            return clkbuiltins.FLOAT32
        return signal_type

    def _preserves_metadata(self, agg_type: signal_module.AggregationType) -> bool:
        """Check if an aggregation type preserves metadata.

        Metadata is preserved for: VALUE, MIN, MAX, FIRST_VALUE, FINAL_VALUE
        Metadata is stripped for: SUM, COUNT, MEAN

        Args:
            agg_type: The aggregation type to check.

        Returns:
            True if the aggregation preserves metadata, False otherwise.
        """
        return agg_type in (
            signal_module.AggregationType.VALUE,
            signal_module.AggregationType.MIN,
            signal_module.AggregationType.MAX,
            signal_module.AggregationType.FIRST_VALUE,
            signal_module.AggregationType.FINAL_VALUE,
        )

    def _expand_mean_to_sum_count(
        self, pre_agg_types: set[signal_module.AggregationType]
    ) -> set[signal_module.AggregationType]:
        """Expand MEAN aggregation to SUM and COUNT for batched report groups.

        For batched report groups, MEAN is stored as SUM and COUNT so that the mean
        can be in post processing.

        Args:
            pre_agg_types: The original set of pre-aggregation types.

        Returns:
            A new set with MEAN replaced by SUM and COUNT.
        """
        if signal_module.AggregationType.MEAN not in pre_agg_types:
            return pre_agg_types

        expanded = set(pre_agg_types)
        expanded.discard(signal_module.AggregationType.MEAN)
        expanded.add(signal_module.AggregationType.SUM)
        expanded.add(signal_module.AggregationType.COUNT)
        return expanded


@dataclass
class ReportGroupConfig:
    """Configuration for a report group."""

    reporting_strategy: ReportingStrategy
    log_type: ReportGroupLogType
    min_observations: int | None
    max_observations: int | None
    min_duration: primitive.UnitValue | None
    max_duration: primitive.UnitValue | None

    @staticmethod
    def from_policy_schema_instance(
        report_group_def: ReportGroupDef,
        policy_schema_instance: schema.SchemaInstance,
    ) -> ReportGroupConfig:
        """Create a ReportGroupConfig from a policy schema instance.

        Args:
            report_group_def: The report group definition for error reporting.
            policy_schema_instance: The instantiated schema representing the report group policy.
        """
        reporting_strategy = _extract_enum_value(
            policy_schema_instance.data,
            "reporting_strategy",
            "reporting strategy",
            {
                "post_aggregated": ReportingStrategy.POST_AGGREGATED,
                "batched": ReportingStrategy.BATCHED,
            },
            report_group_def,
        )

        log_type = _extract_enum_value(
            policy_schema_instance.data,
            "log_type",
            "log type",
            {
                "telemetry": ReportGroupLogType.TELEMETRY,
                "event": ReportGroupLogType.EVENT,
                "none": ReportGroupLogType.NONE,
            },
            report_group_def,
        )

        try:
            max_observations = _extract_int_value(policy_schema_instance.data, "max_observations")
            min_observations = _extract_int_value(policy_schema_instance.data, "min_observations")
            min_duration = _extract_duration_value(policy_schema_instance.data, "min_duration")
            max_duration = _extract_duration_value(policy_schema_instance.data, "max_duration")
        except Exception as e:
            msg = report_group_def.append_error_line(f"Error extracting value: {e}")
            raise ValueError(msg) from e
        if max_observations is None and max_duration is None:
            msg = report_group_def.append_error_line(
                "Report group policy must specify at least one of max_observations or max_duration"
            )
            raise ValueError(msg)
        if reporting_strategy == ReportingStrategy.BATCHED and max_observations is None:
            msg = report_group_def.append_error_line(
                "Batched report group policy must specify max_observations to define the batch size"
            )
            raise ValueError(msg)
        return ReportGroupConfig(
            reporting_strategy=reporting_strategy,
            log_type=log_type,
            min_observations=min_observations,
            max_observations=max_observations,
            min_duration=min_duration,
            max_duration=max_duration,
        )


def _extract_int_value(schema_data: dict[str, typesys.Value], key: str) -> int | None:
    value = schema_data.get(key)
    if not value or isinstance(value, clkbuiltins.Nullopt):
        return None
    if not isinstance(value, primitive.DecimalValue):
        msg = f"Expected integer value for '{key}', but got {value!r}"
        raise TypeError(msg)
    return primitive.unsigned_decimal_to_int(value)


def _extract_duration_value(schema_data: dict[str, typesys.Value], key: str) -> primitive.UnitValue | None:
    value = schema_data.get(key)
    if not value or isinstance(value, clkbuiltins.Nullopt):
        return None
    if not isinstance(value, primitive.UnitValue):
        msg = f"Expected Duration value for '{key}', but got {value!r}"
        raise TypeError(msg)
    if value.type_info is not clkbuiltins.DURATION:
        msg = f"Expected Duration type for '{key}', but got {value.type_info}"
        raise TypeError(msg)
    return value


def _extract_enum_value(
    schema_data: dict[str, typesys.Value],
    field_name: str,
    enum_type_name: str,
    value_map: dict[str, EnumT],
    report_group_def: ReportGroupDef,
) -> EnumT:
    """Extract and convert an enum value from policy schema data.

    Args:
        schema_data: The schema instance data dictionary.
        field_name: The field name to extract.
        enum_type_name: The name of the enum type for error messages.
        value_map: A dictionary mapping enum value names to their corresponding Python enum values.
        report_group_def: The report group definition for error reporting.

    Returns:
        The corresponding Python enum value.

    Raises:
        ValueError: If the field is missing or has an unknown enum value.
        TypeError: If the field is not an EnumValue.
    """
    enum_value = schema_data.get(field_name)
    if enum_value is None:
        msg = f"Report group policy schema instance missing '{field_name}' field"
        raise ValueError(msg)
    if not isinstance(enum_value, clkenum.ValueRef):
        msg = f"'{field_name}' field must be an EnumValue"
        raise TypeError(msg)

    result = value_map.get(enum_value.name)
    if result is None:
        msg = report_group_def.append_error_line(f"Unknown {enum_type_name} enum value: {enum_value.name}")
        raise ValueError(msg)
    return result


@dataclass
class ReportGroupInstance:
    """An instantiated report group with resolved entries."""

    group_def: ReportGroupDef
    entries: dict[str, ReportGroupEntryInstance]
    cog_instance_fqn: str

    def channel_name(self) -> str:
        """Get the channel name for this report group instance."""
        return report_group_instance_channel_name(self.group_def, self.cog_instance_fqn)


def report_group_instance_channel_uuid(report_group: ReportGroupDef, cog_instance_fqn: str) -> uuid.UUID:
    """Generate a stable UUID for a report group instance."""
    return uuid.uuid3(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, f"{cog_instance_fqn}/{report_group.name}")


def report_group_instance_channel_name(report_group: ReportGroupDef, cog_instance_fqn: str) -> str:
    """Generate the channel name for a report group instance."""
    channel_uuid = report_group_instance_channel_uuid(report_group, cog_instance_fqn)
    return f"/_clockwork/report-groups/{report_group.parent_cog_name}/{report_group.name}/{channel_uuid}"


@dataclass
class ReportGroupEntryInstance:
    """An instantiated signal with its instance name."""

    signal: signal_module.ResolvedSignal
    instance_name: str
    cog_private: bool  # True if signal is defined within the cog (not module-level)
    post_aggregation: set[signal_module.AggregationType] = field(default_factory=set)


def _get_interface_info_from_schema(
    module: node.Module, message_schema: schema.InstantiatedSchema
) -> schema_reg.InterfaceInfo:
    schema_arg = message_schema.schema.source
    if schema_arg is None:
        msg = f"{message_schema.schema_name} must have an underlying schema"
        raise ValueError(msg)

    repr_typespec = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE, instantiates=clkbuiltins.TACHYON, arguments={"schema": schema_arg}
    )

    representation_reference = representation.RepresentationReference(message_schema, repr_typespec)

    interface_typesec = typesys.Instantiation(
        clkbuiltins.TYPE_TYPE, instantiates=clkbuiltins.TAP, arguments={"representation": repr_typespec}
    )
    return schema_reg.InterfaceInfo.make(
        interface.InterfaceInstantiation(
            module=module,
            cst_node=None,
            name="",
            scope=module.inner_scope,
            representation=representation_reference,
            is_generic=False,
            typespec=interface_typesec,
        )
    )


def _get_repr_and_interface_from_schema(
    module: node.Module, message_schema: schema.InstantiatedSchema
) -> tuple[representation.ReprInstantiation, interface.InterfaceInstantiation]:
    schema_arg = message_schema.schema.source
    if schema_arg is None:
        msg = f"{message_schema.schema_name} must have an underlying schema"
        raise ValueError(msg)

    resolved_repr = representation.ResolvedReprInstantiation.from_schema(schema_arg, module)

    repr_instantiation = representation.ReprInstantiation(
        module=module,
        cst_node=None,
        name="",
        scope=module.inner_scope,
        schema_ir=message_schema,
        is_generic=False,
        typespec=resolved_repr.typespec,
        resolved=resolved_repr,
    )

    interface_inst = interface.InterfaceInstantiation.from_schema(schema_arg, module, name="")

    return repr_instantiation, interface_inst
