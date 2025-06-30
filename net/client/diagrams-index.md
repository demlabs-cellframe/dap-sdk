# 📊 Architecture Diagrams Index

## 🎯 **Полный набор диаграмм HTTP2 Client архитектуры**

---

## **1. 🏗️ OSI Layer Mapping**
```mermaid
graph TB
    subgraph "OSI Layers"
        OSI7["Application Layer (7)<br/>HTTP/WebSocket/Binary/Custom"]
        OSI6["Presentation Layer (6)<br/>SSL/TLS + Custom Encryption"]
        OSI5["Session Layer (5)<br/>Connection Management"]
        OSI4["Transport Layer (4)<br/>TCP"]
        OSI3["Network Layer (3)<br/>IP"]
    end
    
    subgraph "Our Architecture"
        CLIENT["Client Layer<br/>• UID Management (64-bit composite)<br/>• High-level API<br/>• Cross-thread access<br/>• User Interface"]
        STREAM["Stream Layer<br/>• Protocol Switching<br/>• Channel Dispatching<br/>• Handshake Handlers<br/>• Data Processing"]
        SESSION["Session Layer<br/>• Socket Management<br/>• Unified Encryption Pipeline<br/>• Connection State<br/>• Upgrade Interface"]
        WORKER["Worker Layer<br/>• Event Loop<br/>• Object Ownership<br/>• Memory Management<br/>• Thread Safety"]
    end
    
    subgraph "System Layers"
        EVENTS["dap_events_socket<br/>• Async I/O<br/>• Event Handling<br/>• Socket Abstraction"]
        KERNEL["Kernel<br/>• TCP/IP Stack<br/>• Network Interface<br/>• System Calls"]
    end
    
    CLIENT -.-> OSI7
    STREAM -.-> OSI7
    STREAM -.-> OSI6
    SESSION -.-> OSI6
    SESSION -.-> OSI5
    WORKER -.-> OSI4
    EVENTS -.-> OSI4
    KERNEL -.-> OSI3
    
    CLIENT -->|"UID only"| STREAM
    STREAM -->|"Upgrade Interface"| SESSION
    SESSION -->|"Factory Pattern"| WORKER
    WORKER -->|"Owns all objects"| EVENTS
    EVENTS --> KERNEL
    
    style CLIENT fill:#e1f5fe
    style STREAM fill:#f3e5f5
    style SESSION fill:#e8f5e8
    style WORKER fill:#fff3e0
    style EVENTS fill:#f0f0f0
    style KERNEL fill:#d0d0d0
```

---

## **2. 🎯 Layer Responsibilities**
```mermaid
graph LR
    subgraph "Layer Responsibilities"
        subgraph "🎯 Client Layer"
            C1["UID Management<br/>• Composite 64-bit UID<br/>• Worker ID + Stream ID<br/>• Cross-thread access"]
            C2["High-level API<br/>• Request/Response<br/>• Sync/Async modes<br/>• Error handling"]
            C3["User Interface<br/>• Simple functions<br/>• Configuration<br/>• State queries"]
        end
        
        subgraph "📡 Stream Layer"
            S1["Protocol Processing<br/>• HTTP parsing<br/>• WebSocket frames<br/>• Binary channels<br/>• SSE events"]
            S2["State Management<br/>• HEADERS → UPGRADED → COMPLETE<br/>• Error handling<br/>• State transitions"]
            S3["Handshake Handling<br/>• detect_callback<br/>• handshake_callback<br/>• ready_callback"]
        end
        
        subgraph "🔗 Session Layer"
            SE1["Connection Management<br/>• Socket lifecycle<br/>• Connect/Accept<br/>• Timeouts"]
            SE2["Encryption Management<br/>• TLS/SSL<br/>• Custom encryption<br/>• Unified decryption"]
            SE3["Transport Services<br/>• Data send/receive<br/>• Buffer management<br/>• Error recovery"]
        end
        
        subgraph "⚙️ Worker Layer"
            W1["Event Loop<br/>• epoll/kqueue<br/>• Async I/O<br/>• Timer management"]
            W2["Thread Safety<br/>• Object ownership<br/>• Atomic operations<br/>• Memory barriers"]
            W3["Resource Management<br/>• Memory allocation<br/>• Object lifecycle<br/>• Cleanup"]
        end
    end
    
    C1 --> S1
    C2 --> S2
    C3 --> S3
    S1 --> SE1
    S2 --> SE2
    S3 --> SE3
    SE1 --> W1
    SE2 --> W2
    SE3 --> W3
```

---

## **3. 🔄 Protocol Upgrade with Handshake**
```mermaid
sequenceDiagram
    participant Client as 🎯 Client
    participant Stream as 📡 Stream
    participant Session as 🔗 Session
    participant Worker as ⚙️ Worker
    participant Server as 🌐 Server
    
    Note over Client,Server: Protocol Upgrade Flow with Handshake
    
    Client->>Worker: create_client()
    Worker->>Session: create_session()
    Worker->>Stream: create_stream(handshake_handlers)
    
    Note over Stream: handshake_handlers = {<br/>detect_callback,<br/>handshake_callback,<br/>ready_callback}
    
    Client->>Stream: request("http://dap.server.com/api")
    Stream->>Session: send(HTTP_REQUEST)
    Session->>Server: TCP: "GET /api HTTP/1.1"
    
    Server->>Session: "HTTP/1.1 200 OK<br/>DAP-Encryption: required<br/>DAP-Key-Exchange: needed"
    Session->>Stream: data_received(headers)
    
    Note over Stream: State: HEADERS<br/>Active: detect_callback
    Stream->>Stream: detect_callback()
    Note over Stream: Detects DAP encryption needed
    Stream->>Stream: set_state(UPGRADED)
    
    Note over Stream: State: UPGRADED<br/>Active: handshake_callback
    Stream->>Session: send(DAP_HANDSHAKE_REQUEST)
    Session->>Server: "DAP-HANDSHAKE: client_key_123"
    
    Server->>Session: "DAP-HANDSHAKE-RESPONSE: server_key_456"
    Session->>Stream: data_received(handshake_response)
    Stream->>Stream: handshake_callback()
    
    Note over Stream: Parse encryption keys
    Stream->>Session: upgrade.setup_custom_encryption(keys)
    Session->>Session: encryption_type = CUSTOM<br/>encryption_context = keys
    Session->>Stream: encryption_ready()
    Stream->>Stream: set_state(COMPLETE)
    
    Note over Stream: State: COMPLETE<br/>Active: ready_callback
    Stream->>Session: send(ENCRYPTED_DATA_REQUEST)
    Session->>Session: encrypt_data()
    Session->>Server: encrypted_binary_data
    
    Server->>Session: encrypted_response_data
    Session->>Session: decrypt_data()
    Session->>Stream: data_received(decrypted_data)
    Stream->>Stream: ready_callback()
    Stream->>Client: response_callback(processed_data)
```

---

## **4. 🌊 Single Stream Protocol Upgrade**
```mermaid
sequenceDiagram
    participant Client as 🎯 Client
    participant Stream as 📡 Stream (ONE)
    participant Session as 🔗 Session
    participant Worker as ⚙️ Worker
    participant Server as 🌐 DAP Server
    
    Note over Client,Server: Single Stream Protocol Upgrade Flow
    
    Client->>Worker: create_client()
    Worker->>Session: create_session()
    Worker->>Stream: create_stream(handshake_handlers)
    
    Note over Stream: Protocol: HTTP<br/>handshake_handlers = {<br/>detect_callback,<br/>handshake_callback,<br/>ready_callback}
    
    Client->>Stream: request("http://dap.com/api")
    Stream->>Session: send("GET /api HTTP/1.1<br/>DAP-Client: true")
    Session->>Server: plain HTTP request
    
    Server->>Session: "HTTP/1.1 200 OK<br/>DAP-Encryption: required<br/>DAP-Protocol: binary-v2"
    Session->>Stream: data_received(headers)
    
    Note over Stream: State: HEADERS<br/>Active: detect_callback<br/>Protocol: HTTP
    Stream->>Stream: detect_callback()
    Note over Stream: Detects DAP encryption needed
    Stream->>Stream: set_state(UPGRADED)
    
    Note over Stream: State: UPGRADED<br/>Active: handshake_callback<br/>Protocol: HTTP
    Stream->>Session: send("DAP-HANDSHAKE-REQUEST")
    Server->>Session: "DAP-HANDSHAKE-RESPONSE: key_data"
    Session->>Stream: data_received(handshake_data)
    
    Stream->>Stream: handshake_callback()
    Stream->>Session: upgrade.setup_custom_encryption(key_data)
    Session->>Session: encryption_type = CUSTOM<br/>encryption_context = dap_key
    Session->>Stream: encryption_ready()
    
    Stream->>Stream: switch_protocol(BINARY)
    Stream->>Stream: add_channels([0: control, 1: data, 2: events])
    Stream->>Stream: set_state(COMPLETE)
    
    Note over Stream: State: COMPLETE<br/>Active: ready_callback<br/>Protocol: BINARY + Multi-channel
    
    par Control Channel
        Stream->>Session: send_channel(0, "INIT_SESSION")
        Session->>Session: encrypt_data()
        Session->>Server: [CH:0] encrypted_control
        Server->>Session: [CH:0] encrypted_ack
        Session->>Session: decrypt_data()
        Session->>Stream: data_received(decrypted_data)
        Stream->>Stream: ready_callback()
        Stream->>Stream: dispatch_to_channel(0, "ACK")
    and Data Channel
        Stream->>Session: send_channel(1, large_payload)
        Session->>Session: encrypt_data()
        Session->>Server: [CH:1] encrypted_data
        Server->>Session: [CH:1] encrypted_result
        Session->>Session: decrypt_data()
        Session->>Stream: data_received(decrypted_data)
        Stream->>Stream: ready_callback()
        Stream->>Stream: dispatch_to_channel(1, result)
    and Event Channel
        Server->>Session: [CH:2] encrypted_event
        Session->>Session: decrypt_data()
        Session->>Stream: data_received(decrypted_data)
        Stream->>Stream: ready_callback()
        Stream->>Stream: dispatch_to_channel(2, event)
        Stream->>Client: event_callback(event)
    end
    
    Client->>Client: response_callback(processed_data)
```

---

## **5. 📊 UID Management Architecture**
```mermaid
graph TD
    subgraph "UID Management Architecture"
        subgraph "Client Layer"
            Client["🎯 Client<br/>stream_uid: uint64_t<br/>ONLY stores UID"]
        end
        
        subgraph "Worker Thread"
            Worker["⚙️ Worker<br/>worker_id: 0-255"]
            Session["🔗 Session<br/>Owns transport"]
            Stream["📡 Stream<br/>stream_id: 56-bit"]
        end
        
        subgraph "UID Composition"
            UID["64-bit Composite UID<br/>[8 bits Worker ID][56 bits Stream ID]<br/>Example: 0x0300000000000001<br/>Worker: 3, Stream: 1"]
        end
        
        subgraph "Cross-Thread Access"
            Extract["extract_worker_id(uid)<br/>→ uint8_t worker_id"]
            Route["Route to Worker Thread<br/>by worker_id"]
            Find["Find Stream by<br/>extract_stream_id(uid)"]
        end
    end
    
    Client -->|"Stores only"| UID
    UID -->|"Extract"| Extract
    Extract --> Route
    Route --> Worker
    Worker -->|"Owns"| Session
    Worker -->|"Owns"| Stream
    Stream -->|"Assigned when ready"| UID
    Route --> Find
    Find --> Stream
    
    classDef client fill:#e1f5fe
    classDef worker fill:#e8f5e8
    classDef uid fill:#fff3e0
    classDef access fill:#fce4ec
    
    class Client client
    class Worker,Session,Stream worker
    class UID uid
    class Extract,Route,Find access
```

---

## **6. 🔄 Stream State Machine**
```mermaid
stateDiagram-v2
    [*] --> IDLE
    
    IDLE --> REQUEST_SENT : send_request()
    REQUEST_SENT --> HEADERS : receive_headers()
    
    HEADERS --> BODY : standard_http_callback()<br/>handshake_handlers == NULL
    HEADERS --> UPGRADED : detect_callback()<br/>detects handshake needed
    
    BODY --> COMPLETE : body_complete()
    UPGRADED --> COMPLETE : handshake_callback()<br/>completes key exchange
    
    COMPLETE --> CLOSING : close_request()
    CLOSING --> CLOSED : cleanup()
    CLOSED --> [*]
    
    HEADERS --> ERROR : parse_error()
    UPGRADED --> ERROR : handshake_error()
    COMPLETE --> ERROR : processing_error()
    ERROR --> CLOSED : cleanup()
    
    state HEADERS {
        state "Check Handshake" as check
        [*] --> check
        check --> [*]
    }
    
    state UPGRADED {
        state "Perform Handshake" as handshake
        state "Setup Encryption" as encrypt
        [*] --> handshake
        handshake --> encrypt
        encrypt --> [*]
    }
    
    state COMPLETE {
        state "Process Data" as process
        [*] --> process
        process --> [*]
    }
```

---

## **7. 🔐 Unified Encryption Architecture**
```mermaid
graph TB
    subgraph "Encryption Architecture"
        subgraph "Session Layer"
            EncType["encryption_type enum<br/>NONE | TLS | CUSTOM | TLS_CUSTOM"]
            EncContext["encryption_context<br/>void* pointer"]
            
            subgraph "Decryption Pipeline"
                Decrypt["Universal Decrypt Function"]
                TLSDecrypt["TLS Decrypt<br/>ssl_decrypt()"]
                CustomDecrypt["Custom Decrypt<br/>dap_enc_decrypt()"]
                DoubleDecrypt["Double Decrypt<br/>TLS then Custom"]
            end
        end
        
        subgraph "Stream Layer"
            Handlers["handshake_handlers<br/>detect | handshake | ready"]
            Upgrade["Upgrade Interface<br/>setup_custom_encryption()<br/>is_encrypted()"]
        end
        
        subgraph "Data Flow"
            RawData["Raw Network Data"]
            DecryptedData["Decrypted Data"]
            ProcessedData["Processed Data"]
        end
    end
    
    EncType --> Decrypt
    EncContext --> Decrypt
    
    Decrypt --> TLSDecrypt
    Decrypt --> CustomDecrypt
    Decrypt --> DoubleDecrypt
    
    Handlers --> Upgrade
    Upgrade --> EncType
    Upgrade --> EncContext
    
    RawData --> Decrypt
    TLSDecrypt --> DecryptedData
    CustomDecrypt --> DecryptedData
    DoubleDecrypt --> DecryptedData
    DecryptedData --> ProcessedData
    
    classDef session fill:#f3e5f5
    classDef stream fill:#e1f5fe
    classDef data fill:#e8f5e8
    
    class EncType,EncContext,Decrypt,TLSDecrypt,CustomDecrypt,DoubleDecrypt session
    class Handlers,Upgrade stream
    class RawData,DecryptedData,ProcessedData data
```

---

## **8. 📈 Simple GET Request Flow**
```mermaid
sequenceDiagram
    participant User as 👤 User
    participant Client as 🎯 Client
    participant Stream as 📡 Stream
    participant Session as 🔗 Session
    participant Worker as ⚙️ Worker
    participant Server as 🌐 Server
    
    Note over User,Server: Simple GET Request Flow
    
    User->>Client: dap_http2_client_get_sync("https://api.com/data")
    Client->>Worker: create session + stream
    Worker->>Session: create_session()
    Worker->>Stream: create_stream(HTTP)
    Note over Stream: handshake_handlers = NULL<br/>(standard HTTP)
    
    Client->>Stream: set_read_callback(http_client_callback)
    Stream->>Session: connect("api.com", 443, use_ssl=true)
    Session->>Session: encryption_type = TLS<br/>encryption_context = SSL*
    Session->>Server: TLS handshake
    Server->>Session: TLS established
    Session->>Stream: connected()
    
    Stream->>Session: send("GET /data HTTP/1.1<br/>Host: api.com")
    Session->>Session: ssl_encrypt()
    Session->>Server: encrypted HTTP request
    
    Server->>Session: encrypted HTTP response
    Session->>Session: ssl_decrypt()
    Session->>Stream: data_received("HTTP/1.1 200 OK<br/>...")
    
    Note over Stream: State transitions:<br/>IDLE → HEADERS → BODY → COMPLETE
    Stream->>Stream: http_client_callback()
    Stream->>Stream: parse_headers()
    Stream->>Stream: parse_body()
    Stream->>Client: response_callback(data, status=200)
    
    Client->>User: return (data, size, status_code)
```

---

## **9. 🎯 Complex Single Stream Flow**
```mermaid
sequenceDiagram
    participant User as 👤 User
    participant Client as 🎯 Client
    participant Stream as 📡 Stream (ONE)
    participant Session as 🔗 Session
    participant Worker as ⚙️ Worker
    participant Server as 🌐 DAP Server
    
    Note over User,Server: Complex Single Stream: HTTP → Custom Encryption → Multi-Channel
    
    User->>Client: init_stream_with_handshake(dap_handlers)
    Client->>Worker: create session + stream with handshake
    Worker->>Session: create_session()
    Worker->>Stream: create_stream(handshake_handlers)
    
    Note over Stream: Protocol: HTTP<br/>handshake_handlers = {<br/>detect_callback: dap_detect,<br/>handshake_callback: dap_handshake,<br/>ready_callback: dap_ready}
    
    User->>Client: request_async("http://dap.com/api")
    Stream->>Session: connect("dap.com", 80, use_ssl=false)
    Session->>Session: encryption_type = NONE
    Session->>Server: TCP connection
    
    Stream->>Session: send("GET /api HTTP/1.1<br/>DAP-Client: true")
    Session->>Server: plain HTTP request
    
    Server->>Session: "HTTP/1.1 200 OK<br/>DAP-Encryption: required<br/>DAP-Protocol: binary-v2"
    Session->>Stream: data_received(headers)
    
    Note over Stream: State: HEADERS<br/>Active: detect_callback<br/>Protocol: HTTP
    Stream->>Stream: dap_detect_callback()
    Note over Stream: Detects DAP encryption + binary protocol
    Stream->>Stream: set_state(UPGRADED)
    
    Note over Stream: State: UPGRADED<br/>Active: handshake_callback<br/>Protocol: HTTP
    Stream->>Session: send("DAP-HANDSHAKE-REQUEST")
    Server->>Session: "DAP-HANDSHAKE-RESPONSE: key_data"
    Session->>Stream: data_received(handshake_data)
    
    Stream->>Stream: dap_handshake_callback()
    Stream->>Session: upgrade.setup_custom_encryption(key_data)
    Session->>Session: encryption_type = CUSTOM<br/>encryption_context = dap_key
    Session->>Stream: encryption_ready()
    
    Stream->>Stream: switch_protocol(BINARY)
    Stream->>Stream: add_channels([0: control, 1: data, 2: events])
    Stream->>Stream: set_state(COMPLETE)
    
    Note over Stream: State: COMPLETE<br/>Active: ready_callback<br/>Protocol: BINARY + Multi-channel
    
    par Control Channel
        Stream->>Session: send_channel(0, "INIT_SESSION")
        Session->>Session: encrypt_data()
        Session->>Server: [CH:0] encrypted_control
        Server->>Session: [CH:0] encrypted_ack
        Session->>Session: decrypt_data()
        Session->>Stream: data_received(decrypted_data)
        Stream->>Stream: dap_ready_callback()
        Stream->>Stream: dispatch_to_channel(0, "ACK")
    and Data Channel
        Stream->>Session: send_channel(1, large_payload)
        Session->>Session: encrypt_data()
        Session->>Server: [CH:1] encrypted_data
        Server->>Session: [CH:1] encrypted_result
        Session->>Session: decrypt_data()
        Session->>Stream: data_received(decrypted_data)
        Stream->>Stream: dap_ready_callback()
        Stream->>Stream: dispatch_to_channel(1, result)
    and Event Channel
        Server->>Session: [CH:2] encrypted_event
        Session->>Session: decrypt_data()
        Session->>Stream: data_received(decrypted_data)
        Stream->>Stream: dap_ready_callback()
        Stream->>Stream: dispatch_to_channel(2, event)
        Stream->>Client: event_callback(event)
    end
    
    Client->>User: response_callback(processed_data)
```

---

## 🎯 **Summary**

**9 диаграмм покрывают:**
- ✅ **Структурную архитектуру** (OSI mapping, responsibilities)
- ✅ **UID Management** (composite UID, cross-thread access)
- ✅ **Protocol Upgrades** (handshake, multi-stream)
- ✅ **State Management** (Stream states, transitions)
- ✅ **Encryption Architecture** (unified decryption pipeline)
- ✅ **Data Flow Examples** (simple GET, complex multi-channel)

**Все диаграммы отражают упрощенную архитектуру без избыточных enum'ов!** 🚀 