# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate diagnostics configurations."""

from uuid import uuid5

from clockwork.dsl.composition import diagnostics_config, diagnostics_config_proto, system
from clockwork.dsl.ir import diagnostics
from clockwork.dsl.ir.uuid_reg import lookup_uuid


def gen_diagnostics_configs(
    sys: system.PhysicalSystem,
) -> diagnostics_config_proto.DatabaseInfo:
    """Generate configurations for the multi-publisher channel subscribers in a system.

    Returns:
        Dictionary from CPU domain to dictionary from channel ID string to MultiSubscriberConfig.
    """
    database = diagnostics_config.DatabaseInfo(reporters=[])
    for inst_id, cog in sys.system.cogs.items():
        for member in cog.members:
            if isinstance(member.member, diagnostics.DiagnosticsDef):
                assert isinstance(member.member.group_id, str)
                assert isinstance(member.member.instance_id, str | None)
                database.reporters.append(
                    diagnostics_config.ReporterInfo(
                        id=uuid5(inst_id, member.member.name),
                        name=member.member.group_id,
                        instance=member.member.instance_id or "",
                    )
                )
            elif isinstance(member.member, diagnostics.InfraDiagnosticsDef):
                assert isinstance(member.member.signals, list)
                database.reporters.append(
                    diagnostics_config.ReporterInfo(
                        id=uuid5(inst_id, member.member.name),
                        name=member.member.name,
                        instance=".".join(cog.fqn.rsplit("::", 1)[-1].split(".")[::-1]),
                    )
                )

    for domain in sys.cpu_domains.values():
        for diagnostics_producer_uuid, diagnostics_producer in domain.platform_diagnostics_producers.items():
            database.reporters.append(
                diagnostics_config.ReporterInfo(
                    id=diagnostics_producer_uuid,
                    name=diagnostics_producer.group_id,
                    instance=diagnostics_producer.instance_id,
                )
            )

    entities_with_diags = sys.system.audio_sources.values()
    for entity in entities_with_diags:
        instance: diagnostics.DiagnosticsInstance = entity.diagnostics
        assert isinstance(instance.diagnostics.group_id, str)
        assert isinstance(instance.diagnostics.instance_id, str | None)
        database.reporters.append(
            diagnostics_config.ReporterInfo(
                id=lookup_uuid(sys.system.module.context, instance),
                name=instance.diagnostics.group_id,
                instance=instance.diagnostics.instance_id or "",
            ),
        )

    return database
