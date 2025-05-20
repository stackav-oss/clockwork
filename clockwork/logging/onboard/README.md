# Onboard log

This directory contains the code to read and write the new onboard log format.

The new log format starts with a subset of MCAP functionality and adds new features to improve recovery from corruption and to reduce CPU overhead when writing to the log.

The log format uses XXH3 64 bit hash values computed on each log record to detect and recover from corrupted log files.
Every record in the log is prefixed by a 32 bit magic number and a 32 bit record length field, and each record is followed by a 64 bit XXH3 hash computed over the record header and contents.
The maximum valid record size is limited to 64 MiB to limit the amount of memory needed to read a message when its length field has been corrupted.
The addition of the magic number and checksum allow the reader to detect and recover from corrupted log files while still being able to read all valid records in the log.

The writer uses direct I/O to optimize writing to the log, which means that all write buffers have to be aligned on 512 byte memory boundaries and be a multiple of 512 byte bytes in length.
We want to minimize the amount of data that gets copied to reduce CPU overhead, but we also need to avoid blowing up the size of the log files by adding excessive padding to small messages to align them.
The writer strikes a balance between CPU overhead and the amount of padding required by copying small messages into aligned memory buffers and using zero copy writes for large messages.
A message handle is provided by the runtime environment to ensure that the message data is not overwritten until it has been written to the log.

The reader reads the records from the log file sequentially starting immediately after the log header.
The first step in reading a record is to check the magic number.
If the magic number at the current position is invalid the reader scans the file sequentially until it finds a valid magic number is located.
Then the length field is used to load the remaining part of the record into memory including the hash value at the end.
The hash value is checked against the hash computed from the record header and contents.
If the hashes match the record is valid.
If the hashes do not match then the reader needs to recover by finding the next valid message in the log starting right after the magic number at the current location, repeating as needed until a valid message is located.

The reader also relies on the hash values to recover from I/O errors returned from read operations in the log.
When the reader gets an I/O error (EIO) it bisects the read region to locate the parts of the region that can be read, and fills in the parts that cannot be read with zeros.

## Log format

Each onboard log starts with a special magic number (“#STACK0\n”) to indicate the type and version of the log file, followed by zero or more log records.

Each log record starts with a 32 bit magic number (“\_REC”) followed by a 32 bit record payload length.
All numeric values stored in the log record are in little-endian byte order.
The maximum value for the record payload length is 64 MiB (67,108,864).
Any length greater than that is invalid and indicates that the record header has been corrupted.
Each record payload is followed by a 64 bit XXH3 checksum computed over the record header and payload.

The writer is allowed to pad the log with zeros as needed between records to align writes to 512 byte operations for direct I/O.

### Log record types

The first two bytes in every record payload is a 16 record type.
The next sections define the payload for each record type.
In this section 4+N is used to represent a 4 byte length followed by a variable length string.

#### Schema record

A schema record contains the schema definition messages logged on a channel.
The fields in the schema record are:

- 16 bit record type (must be 1)
- 16 bit schema ID used to refer to this schema from a message record.
- 16 bit schema encoding ID (ros2msg, proto, …)
- 16 bit schema name length
- schema name
- schema definition, encoding specific, should be empty if the schema encoding is undefined.
  Length is calculated from the record length

#### Channel record

A channel record defines a topic recorded in the log.
The fields in the channel record are:

- 16 bit record type (must be 2)
- 16 bit channel ID used to refer to this channel in data records.
- 16 bit schema ID, or zero if no schema is available
- 16 bit compression type enum (none, lz4, zstd)
- 16 bit message encoding enum (raw, cdr, proto, h.265, …).
- channel name, length is calculated from the record length

#### Message record

A message record is written for each message stored in the log.
The fields in the message record are:

- 16 bit record type (must be 3)
- 16 bit channel ID
- 32 bit sequence number (zero if not available)
- 64 bit log time in nanoseconds since the start of the unix epoch
- 64 bit transmit time in nanoseconds since the start of the unix epoch, set to log time if not available.
- 4+N Message header if available, otherwise leave it empty
- Data bytes (length is calculated from total record length)

## Log writer interface

The onboard logger has two types of writers: the telemetry log writer, which writes messages to the log as they are received, and the event log writer, which buffers the messages in memory for a configurable event lead time and then writes the messages to the log only if they are captured by an event.
For simplicity both variants of the log writer implement a common interface.

The writer sends write requests to the log device asynchronously, and will only block if it needs to close the current file and open the next.
The latency for open and close calls is usually less than 10 microseconds under normal circumstances.

The constructor for the log writers includes two MemoryResource instances that are used to allocate all of the memory used by the writer.
One memory resource is used for initialization, the other is used while the writer is running.
The memory for the write buffers is allocated up front based on the maximum message data rate using the initialization memory resource.
The runtime memory resource is used for smaller allocations as needed to store the channel and schema metadata and to allocate storage for file path names and other miscellaneous data.
The memory resources passed to the constructor should be sized so that the log writer does not run out of memory and throw bad_alloc exceptions during initialization or while it is logging.

## Log reader

The log reader reads the messages from the log files stored under a log directory.
The messages are read in the order they were written to the log.
The log reader only returns valid messages.
Messages with invalid checksums and messages with missing metadata are skipped.
The caller needs to query after the reader has reached the end of the log to find out how many errors were encountered reading the log.

The reader will make a best effort to extract all of the information it can from the log files.
The reader needs to handle the possibility that the record containing the channel or schema definition for a message was corrupted and skipped over when reading the log.
When the reader finds a channel record with an invalid schema index or a message record with an invalid channel index the reader attempts to recover the missing metadata by scanning the other log files in the directory searching for the missing information.
If no other files have the missing information the reader will still return the missing messages with a generated channel name “missing_NNN” and empty schema and message encoding fields.
