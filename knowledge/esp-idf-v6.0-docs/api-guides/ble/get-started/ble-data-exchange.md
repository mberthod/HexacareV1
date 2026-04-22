<!-- Source: _sources/api-guides/ble/get-started/ble-data-exchange.rst.txt (ESP-IDF v6.0 documentation) -->

# Data Exchange

This document is the fourth tutorial in the Getting Started series on Bluetooth Low Energy (Bluetooth LE), aiming to provide a brief overview of the data exchange process within Bluetooth LE connections. Subsequently, this tutorial introduces the code implementation of a GATT server, using the `NimBLE_GATT_Server <bluetooth/ble_get_started/nimble/NimBLE_GATT_Server>` example based on the NimBLE host layer stack.

## Learning Objectives

- Understand the data structure details of characteristic data and services
- Learn about different data access operations in GATT
- Learn about the code structure of the `NimBLE_GATT_Server <bluetooth/ble_get_started/nimble/NimBLE_GATT_Server>` example

## GATT Data Characteristics and Services

GATT services are the infrastructure for data exchange between two devices in a Bluetooth LE connection, with the minimum data unit being an attribute. In the section on `Data Representation and Exchange <gatt_att_introduction>`, we briefly introduced the attributes at the ATT layer and the characteristic data, services, and specifications at the GATT layer. Below are details regarding the attribute-based data structure.

### Attributes

An attribute consists of the following four parts:

| No. | Name              | Description                                                                                                  |
|-----|-------------------|--------------------------------------------------------------------------------------------------------------|
| 1   | Handle            | A 16-bit unsigned integer representing the index of the attribute in the `attribute table <attribute_table>` |
| 2   | Type              | ATT attributes use UUID (Universally Unique Identifier) to differentiate types                               |
| 3   | Access Permission | Indicates whether encryption/authorization is needed; whether it is readable or writable                     |
| 4   | Value             | Actual user data or metadata of another attribute                                                            |

There are two types of UUIDs in Bluetooth LE:

1.  16-bit UUIDs defined by SIG
2.  128-bit UUIDs customized by manufacturers

Common characteristic and service UUIDs are provided in SIG's [Assigned Numbers](https://www.bluetooth.com/specifications/assigned-numbers/) standard document, such as:

| Category            | Type Name              | UUID                                  |
|---------------------|------------------------|---------------------------------------|
| Service             | Blood Pressure Service | <span class="title-ref">0x1810</span> |
| Service             | Common Audio Service   | <span class="title-ref">0x1853</span> |
| Characteristic Data | Age                    | <span class="title-ref">0x2A80</span> |
| Characteristic Data | Appearance             | <span class="title-ref">0x2A01</span> |

In fact, the definitions of these services and characteristic data are also provided by the SIG. For example, the value of the Heart Rate Measurement must include a flag field and a heart rate measurement field, and may include fields such as energy expended, RR-interval, and transmission interval, among others. Therefore, these definitions from SIG allow Bluetooth LE devices from different manufacturers to recognize each other's services or characteristic data, enabling cross-manufacturer communication.

Manufacturers' customized 128-bit UUIDs are used for proprietary services or data characteristics, such as the UUID for the LED characteristic in this example: <span class="title-ref">0x00001525-1212-EFDE-1523-785FEABCD123</span>.

### Characteristic Data

A characteristic data item typically consists of the following attributes:

| No. | Type                       | Function                                                                | Notes                                   |
|-----|----------------------------|-------------------------------------------------------------------------|-----------------------------------------|
| 1   | Characteristic Declaration | Contains properties, handle, and UUID info for the characteristic value | UUID is 0x2803, read-only               |
| 2   | Characteristic Value       | user data                                                               | UUID identifies the characteristic type |
| 3   | Characteristic Descriptor  | Additional description for the characteristic data                      | Optional attribute                      |

#### Relationship between Characteristic Declaration and Characteristic Value

Using the Heart Rate Measurement as an example, the relationship between the characteristic declaration and characteristic value is illustrated as follows:

The table below is an attribute table, containing two attributes of the Heart Rate Measurement characteristic. Let's first look at the attribute with handle 0. Its UUID is <span class="title-ref">0x2803</span>, and the access permission is read-only, indicating that this is a characteristic declaration attribute. The attribute value shows that the read/write property is read-only, and the handle points to 1, indicating that the attribute with handle 1 is the value attribute for this characteristic. The UUID is <span class="title-ref">0x2A37</span>, meaning that this characteristic type is Heart Rate Measurement.

Now, let's examine the attribute with handle 1. Its UUID is <span class="title-ref">0x2A37</span>, and the access permission is also read-only, corresponding directly with the characteristic declaration attribute. The value of this attribute consists of flag bits and measurement values, which complies with the SIG specification for Heart Rate Measurement characteristic data.

| Handle | UUID                                  | Permissions | Value                                        | Attribute Type             |
|--------|---------------------------------------|-------------|----------------------------------------------|----------------------------|
| 0      | <span class="title-ref">0x2803</span> | Read-only   | Properties = Read-only                       | Characteristic Declaration |
|        |                                       |             | Handle = 1                                   |                            |
|        |                                       |             | UUID = <span class="title-ref">0x2A37</span> |                            |
| 1      | <span class="title-ref">0x2A37</span> | Read-only   | Flags                                        | Characteristic Value       |
|        |                                       |             | Measurement value                            |                            |

#### Characteristic Descriptors

Characteristic descriptors provide supplementary information about characteristic data. The most common is the Client Characteristic Configuration Descriptor (CCCD). When a characteristic supports server-initiated `data operations <gatt_data_operation>` (notifications or indications), CCCD must be used to describe the relevant information. This is a read-write attribute that allows the GATT client to inform the server whether notifications or indications should be enabled. Writing to this value is also referred to as subscribing or unsubscribing.

The UUID for CCCD is <span class="title-ref">0x2902</span>, and its attribute value contains only 2 bits of information. The first bit indicates whether notifications are enabled, and the second bit indicates whether indications are enabled. By adding the CCCD to the attribute table and providing indication access permissions for the Heart Rate Measurement characteristic data, we obtain the complete form of the Heart Rate Measurement characteristic data in the attribute table as follows:

| Handle | UUID                                  | Permissions   | Value                                        | Attribute Type             |
|--------|---------------------------------------|---------------|----------------------------------------------|----------------------------|
| 0      | <span class="title-ref">0x2803</span> | Read-only     | Properties = Read/Indicate                   | Characteristic Declaration |
|        |                                       |               | Handle = 1                                   |                            |
|        |                                       |               | UUID = <span class="title-ref">0x2A37</span> |                            |
| 1      | <span class="title-ref">0x2A37</span> | Read/Indicate | Flags                                        | Characteristic Value       |
|        |                                       |               | Measurement value                            |                            |
| 2      | <span class="title-ref">0x2902</span> | Read/Write    | Notification status                          | Characteristic Descriptor  |
|        |                                       |               | Indication status                            |                            |

### Services

The data structure of a service can be broadly divided into two parts:

| No. | Name                                 |
|-----|--------------------------------------|
| 1   | Service Declaration Attribute        |
| 2   | Characteristic Definition Attributes |

The three characteristic data attributes mentioned in the `Characteristic Data <characteristic_attributes>` belong to characteristic definition attributes. In essence, the data structure of a service consists of several characteristic data attributes along with a service declaration attribute.

The UUID for the service declaration attribute is 0x2800, which is read-only and holds the UUID identifying the service type. For example, the UUID for the Heart Rate Service is <span class="title-ref">0x180D</span>, so its service declaration attribute can be represented as follows:

| Handle | UUID                                  | Permissions | Value                                 | Attribute Type      |
|--------|---------------------------------------|-------------|---------------------------------------|---------------------|
| 0      | <span class="title-ref">0x2800</span> | Read-only   | <span class="title-ref">0x180D</span> | Service Declaration |

### Attribute Example

The following is an example of a possible attribute table for a GATT server, using the `NimBLE_GATT_Server <bluetooth/ble_get_started/nimble/NimBLE_GATT_Server>` as an illustration. The example includes two services: the Heart Rate Service and the Automation IO Service. The former contains a Heart Rate Measurement characteristic, while the latter includes an LED characteristic. The complete attribute table for the GATT server is as follows:

| Handle | UUID                                                                                                                                  | Permissions   | Value                                                                        | Attribute Type             |
|--------|---------------------------------------------------------------------------------------------------------------------------------------|---------------|------------------------------------------------------------------------------|----------------------------|
| 0      | <span class="title-ref">0x2800</span>                                                                                                 | Read-only     | UUID = <span class="title-ref">0x180D</span>                                 | Service Declaration        |
| 1      | <span class="title-ref">0x2803</span>                                                                                                 | Read-only     | Properties = Read/Indicate                                                   | Characteristic Declaration |
|        |                                                                                                                                       |               | Handle = 2                                                                   |                            |
|        |                                                                                                                                       |               | UUID = <span class="title-ref">0x2A37</span>                                 |                            |
| 2      | <span class="title-ref">0x2A37</span>                                                                                                 | Read/Indicate | Flags                                                                        | Characteristic Value       |
|        |                                                                                                                                       |               | Measurement value                                                            |                            |
| 3      | <span class="title-ref">0x2902</span>                                                                                                 | Read/Write    | Notification status                                                          | Characteristic Descriptor  |
|        |                                                                                                                                       |               | Indication status                                                            |                            |
| 4      | <span class="title-ref">0x2800</span>                                                                                                 | Read-only     | UUID = <span class="title-ref">0x1815</span>                                 | Service Declaration        |
| 5      | <span class="title-ref">0x2803</span>                                                                                                 | Read-only     | Properties = Write-only                                                      | Characteristic Declaration |
|        |                                                                                                                                       |               | Handle = 6                                                                   |                            |
|        |                                                                                                                                       |               | UUID = <span class="title-ref">0x00001525-1212-EFDE-1523-785FEABCD123</span> |                            |
| 6      | <span class="title-ref">0x00001525-1212-EFDE-</span> <span class="title-ref">1523-785FE</span> <span class="title-ref">ABCD123</span> | Write-only    | LED status                                                                   | Characteristic Value       |

When a GATT client first establishes communication with a GATT server, it pulls metadata from the server's attribute table to discover the available services and characteristics. This process is known as *Service Discovery*.

## GATT Data Operations

Data operations refer to accessing characteristic data on a GATT server, which can be mainly categorized into two types:

1.  Client-initiated operations
2.  Server-initiated operations

### Client-initiated Operations

Client-initiated operations include the following three types:

- **Read**  
  - A straightforward operation to pull the current value of a specific characteristic from the GATT server.

- **Write**  
  - Standard write operations require confirmation from the GATT server upon receiving the client's write request and data.

- **Write without response**  
  - This is another form of write operation that does not require server acknowledgment.

### Server-Initiated Operations

Server-initiated operations are divided into two types:

- **Notify**  
  - A GATT server actively pushes data to the client without requiring a confirmation response.

- **Indicate**  
  - Similar to notifications, but this requires confirmation from the client, which makes indication slower than notification.

Although both notifications and indications are initiated by the server, the prerequisite for these operations is that the client has enabled notifications or indications. Therefore, the data exchange process in GATT essentially begins with a client request for data.

## Hands-On Practice

Having grasped the relevant knowledge of GATT data exchange, let’s combine the `NimBLE_GATT_Server <bluetooth/ble_get_started/nimble/NimBLE_GATT_Server>` example code to learn how to build a simple GATT server using the NimBLE protocol stack and put our knowledge into practice.

### Prerequisites

1.  An {IDF_TARGET_NAME} development board
2.  ESP-IDF development environment
3.  The nRF Connect for Mobile application installed on your phone

If you have not completed the ESP-IDF development environment setup, please refer to `IDF Get Started <../../../get-started/index>`.

### Try It Out

Please refer to `Bluetooth LE Introduction Try It Out <nimble_gatt_server_practice>`.

## Code Explanation

### Project Structure Overview

The root directory structure of `NimBLE_GATT_Server <bluetooth/ble_get_started/nimble/NimBLE_GATT_Server>` is identical to that of `NimBLE_Connection <nimble_connection_project_structure>`. Additionally, the <span class="title-ref">main</span> folder includes source code related to the GATT service and simulated heart rate generation.

### Program Behavior Overview

The program behavior of this example is largely consistent with that of `NimBLE_Connection <nimble_connection_project_structure>`, with the difference being that this example adds GATT services and handles access to GATT characteristic data through corresponding callback functions.

### Entry Function

Based on `NimBLE_Connection <nimble_connection_entry_point>`, a process to initialize the GATT service by calling the <span class="title-ref">gatt_svc_init</span> function has been added. Moreover, in addition to the NimBLE thread, a new <span class="title-ref">heart_rate_task</span> thread has been introduced, responsible for the random generation of simulated heart rate measurement data and indication handling. Relevant code is as follows:

``` C
static void heart_rate_task(void *param) {
    /* Task entry log */
    ESP_LOGI(TAG, "heart rate task has been started!");

    /* Loop forever */
    while (1) {
        /* Update heart rate value every 1 second */
        update_heart_rate();
        ESP_LOGI(TAG, "heart rate updated to %d", get_heart_rate());

        /* Send heart rate indication if enabled */
        send_heart_rate_indication();

        /* Sleep */
        vTaskDelay(HEART_RATE_TASK_PERIOD);
    }

    /* Clean up at exit */
    vTaskDelete(NULL);
}

void app_main(void) {
    ...

    xTaskCreate(heart_rate_task, "Heart Rate", 4*1024, NULL, 5, NULL);
    return;
}
```

The <span class="title-ref">heart_rate_task</span> thread runs at a frequency of 1 Hz, as <span class="title-ref">HEART_RATE_TASK_PERIOD</span> is defined as 1000 ms. Each time it executes, the thread calls the <span class="title-ref">update_heart_rate</span> function to randomly generate a new heart rate measurement and then calls <span class="title-ref">send_heart_rate_indication</span> to handle the indication operation.

### GATT Service Initialization

In the <span class="title-ref">gatt_svc.c</span> file, there is a GATT service initialization function as follows:

``` C
int gatt_svc_init(void) {
    /* Local variables */
    int rc;

    /* 1. GATT service initialization */
    ble_svc_gatt_init();

    /* 2. Update GATT services counter */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    /* 3. Add GATT services */
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
```

This function first calls the <span class="title-ref">ble_svc_gatt_init</span> API to initialize the GATT Service. It's important to note that this GATT Service is a special service with the UUID <span class="title-ref">0x1801</span>, which is used by the GATT server to notify clients when services change (i.e., when GATT services are added or removed). In such cases, the client will re-execute the service discovery process to update its service information.

Next, the function calls <span class="title-ref">ble_gatts_count_cfg</span> and <span class="title-ref">ble_gatts_add_svcs</span> APIs to add the services and characteristic data defined in the <span class="title-ref">gatt_svr_svcs</span> service table to the GATT server.

### GATT Service Table

The <span class="title-ref">gatt_svr_svcs service</span> table is a crucial data structure in this example, defining all services and characteristic data used. The relevant code is as follows:

``` C
/* Heart rate service */
static const ble_uuid16_t heart_rate_svc_uuid = BLE_UUID16_INIT(0x180D);

...

static uint16_t heart_rate_chr_val_handle;
static const ble_uuid16_t heart_rate_chr_uuid = BLE_UUID16_INIT(0x2A37);

static uint16_t heart_rate_chr_conn_handle = 0;

...

/* Automation IO service */
static const ble_uuid16_t auto_io_svc_uuid = BLE_UUID16_INIT(0x1815);
static uint16_t led_chr_val_handle;
static const ble_uuid128_t led_chr_uuid =
    BLE_UUID128_INIT(0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, 0xde, 0xef,
                    0x12, 0x12, 0x25, 0x15, 0x00, 0x00);

/* GATT services table */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    /* Heart rate service */
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &heart_rate_svc_uuid.u,
    .characteristics =
        (struct ble_gatt_chr_def[]){
            {/* Heart rate characteristic */
            .uuid = &heart_rate_chr_uuid.u,
            .access_cb = heart_rate_chr_access,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_INDICATE,
            .val_handle = &heart_rate_chr_val_handle},
            {
                0, /* No more characteristics in this service. */
            }}},

    /* Automation IO service */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &auto_io_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){/* LED characteristic */
                                        {.uuid = &led_chr_uuid.u,
                                        .access_cb = led_chr_access,
                                        .flags = BLE_GATT_CHR_F_WRITE,
                                        .val_handle = &led_chr_val_handle},
                                        {0}},
    },

    {
        0, /* No more services. */
    },
};
```

The macros <span class="title-ref">BLE_UUID16_INIT</span> and <span class="title-ref">BLE_UUID128_INIT</span> provided by the NimBLE protocol stack allow for convenient conversion of 16-bit and 128-bit UUIDs from raw data into <span class="title-ref">ble_uuid16_t</span> and <span class="title-ref">ble_uuid128_t</span> type variables.

The <span class="title-ref">gatt_svr_svcs</span> is an array of structures of type <span class="title-ref">ble_gatt_svc_def</span>. The <span class="title-ref">ble_gatt_svc_def</span> structure defines a service, with key fields being <span class="title-ref">type</span>, <span class="title-ref">uuid</span>, and <span class="title-ref">characteristics</span>. The <span class="title-ref">type</span> field indicates whether the service is primary or secondary, with all services in this example being primary. The <span class="title-ref">uuid</span> field represents the UUID of the service. The <span class="title-ref">characteristics</span> field is an array of <span class="title-ref">ble_gatt_chr_def</span> structures that stores the characteristics associated with the service.

The <span class="title-ref">ble_gatt_chr_def</span> structure defines the characteristics, with key fields being <span class="title-ref">uuid</span>, <span class="title-ref">access_cb</span>, <span class="title-ref">flags</span>, and <span class="title-ref">val_handle</span>. The <span class="title-ref">uuid</span> field is the UUID of the characteristic. The <span class="title-ref">access_cb</span> field points to the access callback function for that characteristic. The <span class="title-ref">flags</span> field indicates the access permissions for the characteristic data. The <span class="title-ref">val_handle</span> field points to the variable handle address for the characteristic value.

It's important to note that when the <span class="title-ref">BLE_GATT_CHR_F_INDICATE</span> flag is set for a characteristic, the NimBLE protocol stack automatically adds the CCCD, so there's no need to manually add the descriptor.

Based on variable naming, it's clear that <span class="title-ref">gatt_svr_svcs</span> implements all property definitions in the `attribute table <attribute_table>`. Additionally, access to the Heart Rate Measurement characteristic is managed through the <span class="title-ref">heart_rate_chr_access</span> callback function, while access to the LED characteristic is managed through the <span class="title-ref">led_chr_access</span> callback function.

### Characteristic Data Access Management

#### LED Access Management

Access to the LED characteristic data is managed through the <span class="title-ref">led_chr_access</span> callback function, with the relevant code as follows:

``` C
static int led_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
    /* Local variables */
    int rc;

    /* Handle access events */
    /* Note: LED characteristic is write only */
    switch (ctxt->op) {

    /* Write characteristic event */
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        /* Verify connection handle */
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "characteristic write; conn_handle=%d attr_handle=%d",
                    conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG,
                    "characteristic write by nimble stack; attr_handle=%d",
                    attr_handle);
        }

        /* Verify attribute handle */
        if (attr_handle == led_chr_val_handle) {
            /* Verify access buffer length */
            if (ctxt->om->om_len == 1) {
                /* Turn the LED on or off according to the operation bit */
                if (ctxt->om->om_data[0]) {
                    led_on();
                    ESP_LOGI(TAG, "led turned on!");
                } else {
                    led_off();
                    ESP_LOGI(TAG, "led turned off!");
                }
            } else {
                goto error;
            }
            return rc;
        }
        goto error;

    /* Unknown event */
    default:
        goto error;
    }

error:
    ESP_LOGE(TAG,
            "unexpected access operation to led characteristic, opcode: %d",
            ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}
```

When the GATT client initiates access to the LED characteristic data, the NimBLE protocol stack will call the <span class="title-ref">led_chr_access</span> callback function, passing in the handle information and access context. The <span class="title-ref">op</span> field of <span class="title-ref">ble_gatt_access_ctxt</span> is used to identify different access events. Since the LED is a write-only characteristic, we only handle the <span class="title-ref">BLE_GATT_ACCESS_OP_WRITE_CHR</span> event.

In this processing branch, we first validate the attribute handle to ensure that the client is accessing the LED characteristic. Then, based on the <span class="title-ref">om</span> field of <span class="title-ref">ble_gatt_access_ctxt</span>, we verify the length of the access data. Finally, we check if the data in <span class="title-ref">om_data</span> is equal to 1 to either turn the LED on or off.

If any other access events occur, they are considered unexpected, and we proceed to the error branch to return.

#### Heart Rate Measurement Read Access Management

The heart rate measurement is a readable and indicative characteristic. The read access initiated by the client for heart rate measurement values is managed by the <span class="title-ref">heart_rate_chr_access</span> callback function, with the relevant code as follows:

``` C
static int heart_rate_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg) {
    /* Local variables */
    int rc;

    /* Handle access events */
    /* Note: Heart rate characteristic is read only */
    switch (ctxt->op) {

    /* Read characteristic event */
    case BLE_GATT_ACCESS_OP_READ_CHR:
        /* Verify connection handle */
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "characteristic read; conn_handle=%d attr_handle=%d",
                    conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG, "characteristic read by nimble stack; attr_handle=%d",
                    attr_handle);
        }

        /* Verify attribute handle */
        if (attr_handle == heart_rate_chr_val_handle) {
            /* Update access buffer value */
            heart_rate_chr_val[1] = get_heart_rate();
            rc = os_mbuf_append(ctxt->om, &heart_rate_chr_val,
                                sizeof(heart_rate_chr_val));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        goto error;

    /* Unknown event */
    default:
        goto error;
    }

error:
    ESP_LOGE(
        TAG,
        "unexpected access operation to heart rate characteristic, opcode: %d",
        ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}
```

Similar to the LED access management, we use the <span class="title-ref">op</span> field of the <span class="title-ref">ble_gatt_access_ctxt</span> access context to determine the access event, handling the <span class="title-ref">BLE_GATT_ACCESS_OP_READ_CHR</span> event.

In the handling branch, we first validate the attribute handle to confirm that the client is accessing the heart rate measurement attribute. Then, we call the <span class="title-ref">get_heart_rate</span> function to retrieve the latest heart rate measurement, storing it in the measurement area of the <span class="title-ref">heart_rate_chr_val</span> array. Finally, we copy the data from <span class="title-ref">heart_rate_chr_val</span> into the <span class="title-ref">om</span> field of the <span class="title-ref">ble_gatt_access_ctxt</span> access context. The NimBLE protocol stack will send the data in this field to the client after the current callback function ends, thus achieving read access to the Heart Rate Measurement characteristic value.

#### Heart Rate Measurement Indication

When the client enables indications for heart rate measurements, the processing flow is a bit more complicated. First, enabling or disabling the heart rate measurement indications is a subscription or unsubscription event at the GAP layer, so we need to add a handling branch for subscription events in the <span class="title-ref">gap_event_handler</span> callback function, as follows:

``` C
static int gap_event_handler(struct ble_gap_event *event, void *arg) {
    ...

    /* Subscribe event */
    case BLE_GAP_EVENT_SUBSCRIBE:
        /* Print subscription info to log */
        ESP_LOGI(TAG,
                "subscribe event; conn_handle=%d attr_handle=%d "
                "reason=%d prevn=%d curn=%d previ=%d curi=%d",
                event->subscribe.conn_handle, event->subscribe.attr_handle,
                event->subscribe.reason, event->subscribe.prev_notify,
                event->subscribe.cur_notify, event->subscribe.prev_indicate,
                event->subscribe.cur_indicate);

        /* GATT subscribe event callback */
        gatt_svr_subscribe_cb(event);
        return rc;
}
```

The subscription event is represented by <span class="title-ref">BLE_GAP_EVENT_SUBSCRIBE</span>. In this handling branch, we do not process the subscription event directly; instead, we call the <span class="title-ref">gatt_svr_subscribe_cb</span> callback function to handle the subscription event. This reflects the layered design philosophy of software, as the subscription event affects the GATT server's behavior in sending characteristic data and is not directly related to the GAP layer. Thus, it should be passed to the GATT layer for processing.

Next, let's take a look at the operations performed in the <span class="title-ref">gatt_svr_subscribe_cb</span> callback function.

``` C
void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    /* Check connection handle */
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d",
                event->subscribe.conn_handle, event->subscribe.attr_handle);
    } else {
        ESP_LOGI(TAG, "subscribe by nimble stack; attr_handle=%d",
                event->subscribe.attr_handle);
    }

    /* Check attribute handle */
    if (event->subscribe.attr_handle == heart_rate_chr_val_handle) {
        /* Update heart rate subscription status */
        heart_rate_chr_conn_handle = event->subscribe.conn_handle;
        heart_rate_chr_conn_handle_inited = true;
        heart_rate_ind_status = event->subscribe.cur_indicate;
    }
}
```

In this example, the callback handling is quite simple: it checks whether the attribute handle in the subscription event corresponds to the heart rate measurement attribute handle. If it does, it saves the corresponding connection handle and updates the indication status requested by the client.

As mentioned in `Entry Function <nimble_gatt_server_entry_point>`, the <span class="title-ref">send_heart_rate_indication</span> function is called by the <span class="title-ref">heart_rate_task</span> thread at a frequency of 1 Hz. The implementation of this function is as follows:

``` C
void send_heart_rate_indication(void) {
    if (heart_rate_ind_status && heart_rate_chr_conn_handle_inited) {
        ble_gatts_indicate(heart_rate_chr_conn_handle,
                        heart_rate_chr_val_handle);
        ESP_LOGI(TAG, "heart rate indication sent!");
    }
}
```

The <span class="title-ref">ble_gatts_indicate</span> function is an API provided by the NimBLE protocol stack for sending indications. This means that when the indication status for the heart rate measurement is true and the corresponding connection handle is available, calling the <span class="title-ref">send_heart_rate_indication</span> function will send the heart rate measurement to the GATT client.

To summarize, when a GATT client subscribes to heart rate measurements, the <span class="title-ref">gap_event_handler</span> receives the subscription event and passes it to the <span class="title-ref">gatt_svr_subscribe_cb</span> callback function, which updates the subscription status for heart rate measurements. In the <span class="title-ref">heart_rate_task</span> thread, it checks the subscription status every second; if the status is true, it sends the heart rate measurement to the client.

## Summary

Through this tutorial, you have learned how to create GATT services and their corresponding characteristic data using a service table, and you mastered the management of access to GATT characteristic data, including read, write, and subscription operations. You can now build more complex GATT service applications based on the `NimBLE_GATT_Server <bluetooth/ble_get_started/nimble/NimBLE_GATT_Server>` example.
