# OneNode Protocol v1

## Protocol Frame:

`[4 bytes: N, unsigned, Big Endian][N bytes: UTF8 JSON header][body]`

where N is the header length in bytes

## Header field

- v -> Version number (Integer type)
- type -> Kind of message (String type)

## Type field

This field can contain any of the following value:

### 1. "file" (during sender to receiver):

It contains two fields inturn:

1. field "filename": it contains the name of the file. File name should not be empty (String type) 
2. field "size": it contains the value of the length of the file. Negative size value is not allowed (Integer type)

### 2. "file_result" (during receiver to sender):

- shortly acknowledgement
- it contains two values inturn:

    1. field "status": just gives either "ok" or "error" (String type)
    2. field "code": error string when error (String type)

### Error codes:

- invalid_header
- invalid_filename
- write_failed
- unsupported
    
## Rules

- Header limit: 1 - 16383 bytes (16 KiB)
- Receivers ignore the invalid header fields along with matching error code and close the same
- Unknown type or unsupported "v" should be replied with error "unsupported" and closed
- "ok" is sent only when the file is fully written and named properly
