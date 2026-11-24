/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <common.hpp>
#include <fff.h>
#include <gva_json_meta.h>
#include <gvametapublish.hpp>

#pragma message("gvametapublish test prepared with File support")

#ifdef PAHO_INC
#pragma message("gvametapublish test prepared with MQTT support")
#include <MQTTAsync.h>
#include <uuid/uuid.h>
#endif

#include <test_common.h>
#include <test_utils.h>

DEFINE_FFF_GLOBALS;

// Mocked MQTT Functions
#ifdef PAHO_INC
FAKE_VALUE_FUNC(int, MQTTAsync_create, MQTTAsync *, const char *, const char *, int, void *);
FAKE_VALUE_FUNC(int, MQTTAsync_connect, MQTTAsync, const MQTTAsync_connectOptions *);
FAKE_VALUE_FUNC(int, MQTTAsync_sendMessage, MQTTAsync, const char *, const MQTTAsync_message *,
                MQTTAsync_responseOptions *);
FAKE_VALUE_FUNC(int, MQTTAsync_isConnected, MQTTAsync);
FAKE_VALUE_FUNC(int, MQTTAsync_disconnect, MQTTAsync, const MQTTAsync_disconnectOptions *);
FAKE_VOID_FUNC(MQTTAsync_destroy, MQTTAsync *);
FAKE_VALUE_FUNC(int, MQTTAsync_setCallbacks, MQTTAsync, void *, MQTTAsync_connectionLost *, MQTTAsync_messageArrived *,
                MQTTAsync_deliveryComplete *);
#endif

#include <gst/video/video.h>

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));
typedef struct _GVADetection {
    gfloat x_min;
    gfloat y_min;
    gfloat x_max;
    gfloat y_max;
    gdouble confidence;
    gint label_id;
    gint object_id;
} GVADetection;

struct TestData {
    Resolution resolution;
    GVADetection box;
    uint8_t buffer[8];
    bool ignore_detections;
    std::string method;
    bool metaadd;
    std::string message_payload;
};

char *topic = NULL;
char *payload_message = NULL;

#ifdef PAHO_INC
MQTTAsync_onSuccess *mqtt_send_message_on_success;
MQTTAsync_onFailure *mqtt_send_message_on_failure;
#endif

#ifdef PAHO_INC
int sendMessage_fake(MQTTAsync client, const char *msg_topic, const MQTTAsync_message *msg,
                     MQTTAsync_responseOptions *response_options) {
    int length = strlen(msg_topic) + 1;
    topic = new char[length]();
    g_strlcpy(topic, msg_topic, length);
    int payloadLength = strlen((char *)msg->payload) + 1;
    payload_message = new char[payloadLength]();
    g_strlcpy(payload_message, (char *)msg->payload, payloadLength);
    g_print("Mock sendMessage method received PayloadMessage: [%s]\n", payload_message);
    mqtt_send_message_on_success = response_options->onSuccess;
    mqtt_send_message_on_failure = response_options->onFailure;
    return 0;
}
int mqtt_connect_fake(MQTTAsync client, const MQTTAsync_connectOptions *conn_opts) {
    (void)client;
    if (conn_opts->onSuccess)
        conn_opts->onSuccess(conn_opts->context, NULL);
    if (conn_opts->onFailure)
        conn_opts->onFailure(conn_opts->context, NULL);
    return MQTTASYNC_SUCCESS;
}

int mqtt_setCallbacks_fake(MQTTAsync client, void *context, MQTTAsync_connectionLost *cl, MQTTAsync_messageArrived *ma,
                           MQTTAsync_deliveryComplete *dc) {
    char *test_cause = (char *)"this is the cause from the test";
    if (cl)
        cl(context, test_cause);
    if (ma)
        ma(NULL, NULL, 0, NULL);
    if (dc)
        dc(NULL, 0);
    return MQTTASYNC_SUCCESS;
}

int mqtt_disconnect_fake(MQTTAsync client, const MQTTAsync_disconnectOptions *disconn_opts) {
    if (mqtt_send_message_on_success)
        mqtt_send_message_on_success(NULL, NULL);
    if (mqtt_send_message_on_failure)
        mqtt_send_message_on_failure(NULL, NULL);
    if (disconn_opts->onSuccess)
        disconn_opts->onSuccess(NULL, NULL);
    if (disconn_opts->onFailure)
        disconn_opts->onFailure(NULL, NULL);
    return MQTTASYNC_SUCCESS;
}

#endif

void setup_inbuffer(GstBuffer *inbuffer, gpointer user_data) {
    TestData *test_data = static_cast<TestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not TestData");
    GstStructure *s =
        gst_structure_new("detection", "confidence", G_TYPE_DOUBLE, test_data->box.confidence, "label_id", G_TYPE_INT,
                          test_data->box.label_id, "precision", G_TYPE_INT, 10, "x_min", G_TYPE_DOUBLE,
                          test_data->box.x_min, "x_max", G_TYPE_DOUBLE, test_data->box.x_max, "y_min", G_TYPE_DOUBLE,
                          test_data->box.y_min, "y_max", G_TYPE_DOUBLE, test_data->box.y_max, "model_name",
                          G_TYPE_STRING, "model_name", "layer_name", G_TYPE_STRING, "layer_name", NULL);
    GVariant *v = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, test_data->buffer, 8, 1);
    gsize n_elem;
    gst_structure_set(s, "data_buffer", G_TYPE_VARIANT, v, "data", G_TYPE_POINTER,
                      g_variant_get_fixed_array(v, &n_elem, 1), NULL);
    if (test_data->metaadd) {
        if (!gst_buffer_is_writable(inbuffer))
            throw std::runtime_error("Buffer is not writable.");

        GstGVAJSONMeta *meta2 = GST_GVA_JSON_META_ADD(inbuffer);
        if (test_data->message_payload != "") {
            meta2->message = strdup(test_data->message_payload.c_str());
        } else {
            meta2->message = nullptr;
        }
    }
}

void reset_mock_functions() {
    if (payload_message != NULL) {
        g_print("Test Error: payload_message was not freed during test!");
        g_print("Will now cleanup by freeing payload_message that holds value: [%s]\n", payload_message);
        free(payload_message);
        payload_message = NULL;
    }
    if (topic != NULL) {
        g_print("Test Error: topic was not freed during test!");
        g_print("Will now cleanup by freeing topic that holds value: [%s]\n", topic);
        free(topic);
        topic = NULL;
    }

#ifdef PAHO_INC
    RESET_FAKE(MQTTAsync_create);
    RESET_FAKE(MQTTAsync_connect);
    RESET_FAKE(MQTTAsync_sendMessage);
    RESET_FAKE(MQTTAsync_isConnected);
    RESET_FAKE(MQTTAsync_disconnect);
    RESET_FAKE(MQTTAsync_destroy);
    RESET_FAKE(MQTTAsync_setCallbacks);
#endif

    FFF_RESET_HISTORY();
}

TestData test_data[] = {
    {{640, 480}, {0.29375, 0.54375, 0.40625, 0.94167, 0.8, 0, 0}, {0x7c, 0x94, 0x06, 0x3f, 0x09, 0xd7, 0xf2, 0x3e}}};

GST_START_TEST(test_metapublish_file_format_json) {
    // Run the test
    g_print("Starting test: %s", "test_metapublish_file_format_json\n");
    std::vector<std::string> supported_fp = {"FP32"};
    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        for (const auto &fp : supported_fp) {
            test_data[i].method = "all";
            test_data[i].metaadd = true;
            test_data[i].message_payload = "FakeFileMessage";
            run_test("gvametapublish", VIDEO_CAPS_TEMPLATE_STRING, test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, NULL, &test_data[i], "method", GVA_META_PUBLISH_FILE, "file-format",
                     GVA_META_PUBLISH_JSON, "file-path", "metapublish_test_files/metapublish_test.txt", NULL);
        }
    }
}
GST_END_TEST;

GST_START_TEST(test_metapublish_file_no_message) {
    // Run the test
    g_print("Starting test: %s", "test_metapublish_file_no_message\n");
    std::vector<std::string> supported_fp = {"FP32"};
    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        for (const auto &fp : supported_fp) {
            test_data[i].method = "all";
            test_data[i].metaadd = true;
            run_test("gvametapublish", VIDEO_CAPS_TEMPLATE_STRING, test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, NULL, &test_data[i], "method", GVA_META_PUBLISH_FILE, "file-format",
                     GVA_META_PUBLISH_JSON, "file-path", "metapublish_test_files/metapublish_test.txt", NULL);
        }
    }
}
GST_END_TEST;

#ifdef PAHO_INC

GST_START_TEST(test_metapublish_mqtt) {
    reset_mock_functions();

    const char *topic_published = "MQTTtest";
    // Expected value to compare with actual payload received
    const char *msg_published = "FakeMQTTMsg1";
    // Set mock return values
    MQTTAsync_connect_fake.return_val = 0;
    MQTTAsync_isConnected_fake.return_val = 1;
    MQTTAsync_sendMessage_fake.custom_fake = sendMessage_fake;
    // Run the test
    g_print("Starting test: %s", "test_metapublish_mqtt\n");
    std::vector<std::string> supported_fp = {"FP32"};
    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        for (const auto &fp : supported_fp) {
            test_data[i].method = "all";
            test_data[i].metaadd = true;
            // This test is expected to release and nullify payload_message
            // populated here, before the next test invokes reset_mock_functions.
            test_data[i].message_payload = msg_published;
            run_test("gvametapublish", VIDEO_CAPS_TEMPLATE_STRING, test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, NULL, &test_data[i], "method", GVA_META_PUBLISH_MQTT, "address", "172.0.0.1:1883",
                     "mqtt-client-id", "4", "topic", "MQTTtest", NULL);
        }
    }
    // Check that mock functions were called
    ck_assert_msg(MQTTAsync_create_fake.call_count == 1,
                  "Expected create to be called 1 time. It was called %d times.\n", MQTTAsync_create_fake.call_count);
    ck_assert_msg(MQTTAsync_connect_fake.call_count == 1,
                  "Expected connect to be called 1 time. It was called %d times.\n", MQTTAsync_connect_fake.call_count);
    ck_assert_msg(MQTTAsync_sendMessage_fake.call_count == 1,
                  "Expected sendMessage to be called 1 time. It was called %d times.\n",
                  MQTTAsync_sendMessage_fake.call_count);
    if (MQTTAsync_sendMessage_fake.call_count > 0) {
        ck_assert_msg(strcmp(topic, topic_published) == 0,
                      "Topic that was sent to MQTT did not match topic provided to function. Received %s.\n", topic);
        free(topic);
        topic = NULL;
        ck_assert_msg(strcmp(payload_message, msg_published) == 0,
                      "Metadata that was sent to MQTT did not match metadata provided to element. Received %s.\n",
                      payload_message);
        free(payload_message);
        payload_message = NULL;
    }

    ck_assert_msg(MQTTAsync_isConnected_fake.call_count == 1,
                  "Expected isConnected to be called 1 time. It was called %d times.\n",
                  MQTTAsync_isConnected_fake.call_count);
    ck_assert_msg(MQTTAsync_disconnect_fake.call_count == 1,
                  "Expected disconnect to be called 1 time. It was called %d times.\n",
                  MQTTAsync_disconnect_fake.call_count);
    ck_assert_msg(MQTTAsync_destroy_fake.call_count == 1,
                  "Expected destroy to be called 1 time. It was called %d times.\n", MQTTAsync_destroy_fake.call_count);
}

GST_END_TEST;

GST_START_TEST(test_metapublish_mqtt_callbacks) {
    reset_mock_functions();

    const char *topic_published = "MQTTtest";
    // Expected value to compare with actual payload received
    const char *msg_published = "FakeMQTTMsg1";
    // Set mock return values
    MQTTAsync_connect_fake.custom_fake = mqtt_connect_fake;
    MQTTAsync_setCallbacks_fake.custom_fake = mqtt_setCallbacks_fake;
    MQTTAsync_isConnected_fake.return_val = 1;
    MQTTAsync_sendMessage_fake.custom_fake = sendMessage_fake;
    MQTTAsync_disconnect_fake.custom_fake = mqtt_disconnect_fake;
    // Run the test
    g_print("Starting test: %s", "test_metapublish_mqtt_callbacks\n");
    std::vector<std::string> supported_fp = {"FP32"};
    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        for (const auto &fp : supported_fp) {
            test_data[i].method = "all";
            test_data[i].metaadd = true;
            // This test is expected to release and nullify payload_message
            // populated here, before the next test invokes reset_mock_functions.
            test_data[i].message_payload = msg_published;
            run_test("gvametapublish", VIDEO_CAPS_TEMPLATE_STRING, test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, NULL, &test_data[i], "method", GVA_META_PUBLISH_MQTT, "max-connect-attempts", 2,
                     "address", "172.0.0.1:1883", "mqtt-client-id", "4", "topic", "MQTTtest", NULL);
        }
    }
    // Check that mock functions were called
    ck_assert_msg(MQTTAsync_create_fake.call_count == 1,
                  "Expected create to be called 1 time. It was called %d times.\n", MQTTAsync_create_fake.call_count);
    ck_assert_msg(MQTTAsync_setCallbacks_fake.call_count == 1,
                  "Expected setCallbacks to be called 1 time. It was called %d times.\n",
                  MQTTAsync_setCallbacks_fake.call_count);
    ck_assert_msg(MQTTAsync_connect_fake.call_count == 2,
                  "Expected connect to be called 2 times. It was called %d times.\n",
                  MQTTAsync_connect_fake.call_count);
    ck_assert_msg(MQTTAsync_sendMessage_fake.call_count == 1,
                  "Expected sendMessage to be called 1 time. It was called %d times.\n",
                  MQTTAsync_sendMessage_fake.call_count);
    if (MQTTAsync_sendMessage_fake.call_count > 0) {
        ck_assert_msg(strcmp(topic, topic_published) == 0,
                      "Topic that was sent to MQTT did not match topic provided to function. Received %s.\n", topic);
        free(topic);
        topic = NULL;
        ck_assert_msg(strcmp(payload_message, msg_published) == 0,
                      "Metadata that was sent to MQTT did not match metadata provided to element. Received %s.\n",
                      payload_message);
        free(payload_message);
        payload_message = NULL;
    }

    ck_assert_msg(MQTTAsync_isConnected_fake.call_count == 1,
                  "Expected isConnected to be called 1 time. It was called %d times.\n",
                  MQTTAsync_isConnected_fake.call_count);
    ck_assert_msg(MQTTAsync_disconnect_fake.call_count == 1,
                  "Expected disconnect to be called 1 time. It was called %d times.\n",
                  MQTTAsync_disconnect_fake.call_count);
    ck_assert_msg(MQTTAsync_destroy_fake.call_count == 1,
                  "Expected destroy to be called 1 time. It was called %d times.\n", MQTTAsync_destroy_fake.call_count);
}

GST_END_TEST;

// Testing to check if msg does not match
GST_START_TEST(test_metapublish_mqtt_bad_msg_published) {
    reset_mock_functions();
    const char *topic_published = "MQTTtest";
    const char *msg_published = "BadMessage";
    // Assign an arbitrary value to assure tests are not artificially
    // constrained to checking static payloads.
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    char uuid[37]; // 36 character UUID string plus terminating character
    uuid_unparse(binuuid, uuid);
    char *arbitrary_value = uuid;

    // Set mock return values
    MQTTAsync_connect_fake.return_val = 0;
    MQTTAsync_isConnected_fake.return_val = 1;
    MQTTAsync_sendMessage_fake.custom_fake = sendMessage_fake;
    // Run the test
    g_print("Starting test: %s", "test_metapublish_mqtt_bad_msg_published\n");
    std::vector<std::string> supported_fp = {"FP32"};
    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        for (const auto &fp : supported_fp) {
            test_data[i].method = "all";
            test_data[i].metaadd = true;
            // This test is expected to release and nullify payload_message
            // populated here, before the next test invokes reset_mock_functions.
            test_data[i].message_payload = arbitrary_value; // this is the message to be published
            run_test("gvametapublish", VIDEO_CAPS_TEMPLATE_STRING, test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, NULL, &test_data[i], "method", GVA_META_PUBLISH_MQTT, "address", "172.0.0.1:1883",
                     "mqtt-client-id", "4", "topic", "MQTTtest", NULL);
        }
    }
    // Check that mock functions were called
    ck_assert_msg(MQTTAsync_create_fake.call_count == 1,
                  "Expected create to be called 1 time. It was called %d times.\n", MQTTAsync_create_fake.call_count);
    ck_assert_msg(MQTTAsync_connect_fake.call_count == 1,
                  "Expected connect to be called 1 time. It was called %d times.\n", MQTTAsync_connect_fake.call_count);
    ck_assert_msg(MQTTAsync_sendMessage_fake.call_count == 1,
                  "Expected sendMessage to be called 1 time. It was called %d times.\n",
                  MQTTAsync_sendMessage_fake.call_count);
    if (MQTTAsync_sendMessage_fake.call_count > 0) {
        // g_print("Topic: %s\n", topic);
        ck_assert_msg(strcmp(topic, topic_published) == 0,
                      "Topic that was sent to MQTT did not match topic provided to function. Received %s.\n", topic);
        free(topic);
        topic = NULL;
        ck_assert_msg(strcmp(payload_message, msg_published) != 0,
                      "Expected generated uuid Metadata to not match published Metadata. Received %s.\n",
                      payload_message);
        ck_assert_msg(strcmp(payload_message, arbitrary_value) == 0,
                      "Expected generated uuid Metadata to match published Metadata. Received %s.\n", payload_message);
        free(payload_message);
        payload_message = NULL;
    }
    ck_assert_msg(MQTTAsync_isConnected_fake.call_count == 1,
                  "Expected isConnected to be called 1 time. It was called %d times.\n",
                  MQTTAsync_isConnected_fake.call_count);
    ck_assert_msg(MQTTAsync_disconnect_fake.call_count == 1,
                  "Expected disconnect to be called 1 time. It was called %d times.\n",
                  MQTTAsync_disconnect_fake.call_count);
    ck_assert_msg(MQTTAsync_destroy_fake.call_count == 1,
                  "Expected destroy to be called 1 time. It was called %d times.\n", MQTTAsync_destroy_fake.call_count);
}

GST_END_TEST;

GST_START_TEST(test_metapublish_mqtt_no_meta) {
    reset_mock_functions();

    // Set mock return values
    MQTTAsync_connect_fake.return_val = 0;
    MQTTAsync_isConnected_fake.return_val = 1;
    // Run the test
    g_print("Starting test: %s", "test_metapublish_mqtt_no_meta\n");
    std::vector<std::string> supported_fp = {"FP32"};
    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        for (const auto &fp : supported_fp) {
            test_data[i].method = "all";
            test_data[i].metaadd = false;
            run_test("gvametapublish", VIDEO_CAPS_TEMPLATE_STRING, test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, NULL, &test_data[i], "method", GVA_META_PUBLISH_MQTT, "address", "172.0.0.1:1883",
                     "mqtt-client-id", "4", "topic", "MQTTtest", NULL);
        }
    }
    // Check that mock functions were called
    ck_assert_msg(MQTTAsync_create_fake.call_count == 1,
                  "Expected create to be called 1 time. It was called %d times.\n", MQTTAsync_create_fake.call_count);
    ck_assert_msg(MQTTAsync_connect_fake.call_count == 1,
                  "Expected connect to be called 1 time. It was called %d times.\n", MQTTAsync_connect_fake.call_count);
    if (MQTTAsync_sendMessage_fake.call_count > 0) {
        free(payload_message);
        payload_message = NULL;
    }
    ck_assert_msg(MQTTAsync_sendMessage_fake.call_count == 0,
                  "Expected sendMessage not to be called. It was called %d times.\n",
                  MQTTAsync_sendMessage_fake.call_count);
    ck_assert_msg(MQTTAsync_disconnect_fake.call_count == 1,
                  "Expected disconnect to be called 1 time. It was called %d times.\n",
                  MQTTAsync_disconnect_fake.call_count);
    ck_assert_msg(MQTTAsync_destroy_fake.call_count == 1,
                  "Expected destroy to be called 1 time. It was called %d times.\n", MQTTAsync_destroy_fake.call_count);
}

GST_END_TEST;

GST_START_TEST(test_metapublish_mqtt_no_client_id) {
    reset_mock_functions();

    const char *topic_published = "MQTTtest";
    // Expected value to compare with actual payload received
    const char *msg_published = "FakeMQTTMsg1";
    // Set mock return values
    MQTTAsync_connect_fake.return_val = 0;
    MQTTAsync_isConnected_fake.return_val = 1;
    MQTTAsync_sendMessage_fake.custom_fake = sendMessage_fake;
    // Run the test
    g_print("Starting test: %s", "test_metapublish_mqtt_no_client_id\n");
    std::vector<std::string> supported_fp = {"FP32"};
    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        for (const auto &fp : supported_fp) {
            test_data[i].method = "all";
            test_data[i].metaadd = true;
            // This test is expected to release and nullify payload_message
            // populated here, before the next test invokes reset_mock_functions.
            test_data[i].message_payload = msg_published;
            run_test("gvametapublish", VIDEO_CAPS_TEMPLATE_STRING, test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, NULL, &test_data[i], "method", GVA_META_PUBLISH_MQTT, "address", "172.0.0.1:1883",
                     "topic", "MQTTtest", NULL);
        }
    }
    // Check that mock functions were called
    ck_assert_msg(MQTTAsync_create_fake.call_count == 1,
                  "Expected create to be called 1 time. It was called %d times.\n", MQTTAsync_create_fake.call_count);
    ck_assert_msg(MQTTAsync_connect_fake.call_count == 1,
                  "Expected connect to be called 1 time. It was called %d times.\n", MQTTAsync_connect_fake.call_count);
    ck_assert_msg(MQTTAsync_sendMessage_fake.call_count == 1,
                  "Expected sendMessage to be called 1 time. It was called %d times.\n",
                  MQTTAsync_sendMessage_fake.call_count);
    if (MQTTAsync_sendMessage_fake.call_count > 0) {
        ck_assert_msg(strcmp(topic, topic_published) == 0,
                      "Topic that was sent to MQTT did not match topic provided to function. Received %s.\n", topic);
        free(topic);
        ck_assert_msg(strcmp(payload_message, msg_published) == 0,
                      "Metadata that was sent to MQTT did not match metadata provided to element. Received %s.\n",
                      payload_message);
        free(payload_message);
        payload_message = NULL;
    }

    ck_assert_msg(MQTTAsync_isConnected_fake.call_count == 1,
                  "Expected isConnected to be called 1 time. It was called %d times.\n",
                  MQTTAsync_isConnected_fake.call_count);
    ck_assert_msg(MQTTAsync_disconnect_fake.call_count == 1,
                  "Expected disconnect to be called 1 time. It was called %d times.\n",
                  MQTTAsync_disconnect_fake.call_count);
    ck_assert_msg(MQTTAsync_destroy_fake.call_count == 1,
                  "Expected destroy to be called 1 time. It was called %d times.\n", MQTTAsync_destroy_fake.call_count);
}

GST_END_TEST;

#endif

static Suite *metapublish_suite(void) {
    Suite *s = suite_create("metapublish");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_metapublish_file_format_json);
    tcase_add_test(tc_chain, test_metapublish_file_no_message);

#ifdef PAHO_INC
    tcase_add_test(tc_chain, test_metapublish_mqtt);
    tcase_add_test(tc_chain, test_metapublish_mqtt_callbacks);
    tcase_add_test(tc_chain, test_metapublish_mqtt_bad_msg_published);
    tcase_add_test(tc_chain, test_metapublish_mqtt_no_meta);
    tcase_add_test(tc_chain, test_metapublish_mqtt_no_client_id);
#endif

    return s;
}

GST_CHECK_MAIN(metapublish);
