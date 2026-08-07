#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_http_server.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    httpd_handle_t server_handle_;
    void (*motor_control_callback)(int direction, int speed);
    void (*emotion_callback)(const char* emotion);
    struct MotorActionConfig {
        int forward_duration_ms;
        int backward_duration_ms;
        int left_turn_duration_ms;
        int right_turn_duration_ms;
        int spin_duration_ms;
        int wiggle_duration_ms;
        int dance_duration_ms;
        int quick_forward_duration_ms;
        int quick_backward_duration_ms;
        int default_speed_percent;
    } (*get_motor_config_callback)();
    void (*set_motor_config_callback)(const struct MotorActionConfig* config);
} WebServer;

void web_server_start(WebServer* ws, int port);
void web_server_stop(WebServer* ws);
void web_server_set_motor_control_callback(WebServer* ws, void (*callback)(int, int));
void web_server_set_emotion_callback(WebServer* ws, void (*callback)(const char*));
void web_server_set_motor_action_config_callback(WebServer* ws, struct MotorActionConfig (*get_cb)(), void (*set_cb)(const struct MotorActionConfig*));

esp_err_t web_server_index_get_handler(httpd_req_t *req);
esp_err_t web_server_control_post_handler(httpd_req_t *req);
esp_err_t web_server_api_control_handler(httpd_req_t *req);
esp_err_t web_server_debug_motor_test_handler(httpd_req_t *req);
esp_err_t web_server_config_get_handler(httpd_req_t *req);
esp_err_t web_server_config_post_handler(httpd_req_t *req);
esp_err_t web_server_api_config_get_handler(httpd_req_t *req);
esp_err_t web_server_api_config_post_handler(httpd_req_t *req);

const char* web_server_get_html_page();
const char* web_server_get_config_html_page();

void web_server_parse_simple_control_command(const char* data, int* direction, int* speed);
void web_server_parse_config_form_data(const char* data, struct MotorActionConfig* config);

#endif // WEB_SERVER_H
