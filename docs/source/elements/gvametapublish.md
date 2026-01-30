# gvametapublish

Publishes the JSON metadata to MQTT or Kafka message brokers or files.
MQTT and Kafka methods offer reconnection in case the connection to the
broker is lost, or cannot be established. During this reconnection time, any
metadata that passes through the element will not be cached or
published. It will simply pass through to the next element in the
pipeline.

```bash
Pad Templates:
  SRC template: 'src'
    Availability: Always
    Capabilities:
      ANY

  SINK template: 'sink'
    Availability: Always
    Capabilities:
      ANY

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:
  address             : [method= kafka | mqtt] The address of the MQTT server, including the port, e.g. 'localhost:1883'
                        flags: readable, writable
                        String. Default: null
  async-handling      : The bin will handle Asynchronous state changes
                        flags: readable, writable
                        Boolean. Default: false
  file-format         : [method= file] Structure of JSON objects in the file
                        flags: readable, writable
                        Enum "GstGVAMetaPublishFileFormat" Default: 1, "json"
                          (1): json             - the whole file is valid JSON array where each element is inference results per frame
                          (2): json-lines       - each line is valid JSON with inference results per frame
  file-path           : [method= file] Absolute path to output file for publishing inferences.
                        flags: readable, writable
                        String. Default: "stdout"
  max-connect-attempts: [method= kafka | mqtt] Maximum number of failed connection attempts before it is considered fatal. When it is set to -1, the client will try to reconnect indefinitely.
                        flags: readable, writable
                        Unsigned Integer. Range: 1 - 10 Default: 1
  max-reconnect-interval: [method= kafka | mqtt] Maximum time in seconds between reconnection attempts. Initial interval is 1 second and will be doubled on each failure up to this maximum interval.
                        flags: readable, writable
                        Unsigned Integer. Range: 1 - 300 Default: 30
  method              : Publishing method. Set to one of: 'file', 'mqtt', 'kafka'
                        flags: readable, writable
                        Enum "GstGVAMetaPublishMethod" Default: 1, "file"
                          (1): file             - File publish
                          (2): mqtt             - MQTT publish
                          (3): kafka            - Kafka publish
  mqtt-client-id      : [method= mqtt] Unique identifier for the MQTT client. If not provided, one will be generated for you.
                        flags: readable, writable
                        String. Default: null
  mqtt-config         : Path to the JSON file with MQTT configuration. Required for TLS-secured MQTT connections. See the config file description below.
                        flags: readable, writable
                        String. Default: null
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvametapublish0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  topic               : [method= kafka | mqtt] Topic on which to send broker messages
                        flags: readable, writable
                        String. Default: null
```

The MQTT configuration file used with the `mqtt-config` property should
conform to the following JSON schema. Values specified in the
configuration file override values assigned to the individual properties
listed above.

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "MQTT Configuration",
  "description": "Schema for the MQTT configuration file used with the mqtt-config property",
  "type": "object",
  "properties": {
    "address": {
      "type": "string",
      "description": "The address of the MQTT server, including the port, e.g. 'localhost:8883'"
    },
    "client-id": {
      "type": ["string", "null"],
      "description": "The client identifier for the MQTT connection. If it is set to null, the client will generate a unique ID. Default: null."
    },
    "topic": {
      "type": ["string", "null"],
      "description": "The MQTT topic to which the client will publish. Default: null."
    },
    "max-connect-attempts": {
      "type": "integer",
      "description": "The maximum number of connection attempts before giving up. When it is set to -1, the client will try to reconnect indefinitely. Default: 1."
    },
    "max-reconnect-interval": {
      "type": "integer",
      "description": "The maximum interval (in seconds) between reconnection attempts. Default: 30."
    },
    "TLS": {
      "type": "boolean",
      "description": "A boolean indicating whether TLS encryption is enabled. Default: false."
    },
    "ssl_verify": {
      "type": "integer",
      "description": "An integer indicating whether to carry out post-connect checks, including that a certificate matches the given host name. A value of 0 means verification is disabled. Default: 0."
    },
    "ssl_enable_server_cert_auth": {
      "type": "integer",
      "description": "An integer indicating whether to enable server certificate authentication. A value of 0 means it is disabled. Default: 0."
    },
    "ssl_CA_certificate": {
      "type": ["string", "null"],
      "description": "The path to the CA (Certificate Authority) certificate file used to verify the server's certificate. Default: null."
    },
    "ssl_client_certificate": {
      "type": ["string", "null"],
      "description": "The path to the client's SSL certificate file. Default: null."
    },
    "ssl_private_key": {
      "type": ["string", "null"],
      "description": "The path to the client's private key file. Default: null."
    }
  },
  "required": ["address"]
}
```

**Warning:**

This element leverages TLS to ensure secure communication while sending
MQTT messages. The paths to the necessary certificates and keys,
included in the configuration file, are passed to the Paho Asynchronous
MQTT C Client Library for establishing TLS connections. However, it is
important to note that storage, management, and protection of these
certificates and keys are beyond the scope of this component.

**Key Points:**

- **Certificate and Key Handling:** The plugin passes the
paths to the certificates and keys to the Paho library, but does not
handle their storage or protection.
- **Responsibility:** The
responsibility for securely storing and protecting the certificates and
keys (ensuring confidentiality and integrity for private keys and
integrity for public keys) lies with the external application or system
that uses this element. Additionally, the external application is
responsible for generation, provisioning, and removal of keys,
ensuring that these processes are conducted securely and in accordance
with best practices.
- **Security Measures:** It is recommended that
the external application implements appropriate security measures to
ensure confidentiality, integrity, and availability of
cryptographic credentials.
