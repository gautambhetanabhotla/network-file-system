## Sending requests to the storage server
- READ: Pad the "READ" string until 10 bytes and send. Send the virtual path after (padded to 4096 bytes).
- WRITE: 10 bytes for "WRITE", 4096 for virtual path, 20 bytes for last modified time, 20 bytes for content length, and content to be sent in chunks of 4096 bytes
- COPY: 10 bytes, 4096 for source path, 4096 for destination path
- STREAM: 4096 bytes for virtual path
- CREATE: 4096 bytes for virtual path, 20 bytes for last modified time (basically creation time here)