# Network file system from scratch

- Max connections a socket can handle: 100
- For creating a file/directory, the differentiation is done by the name. A name ending in a `/` will be the name of a directory, otherwise it's the name of a file.