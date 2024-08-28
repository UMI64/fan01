#include "leida_web.hpp"
#include "web/index.h"
#include "web/config.h"

leida_web::leida_web(/* args */)
{
    /* index 的 URI 处理结构 */
    httpd_uri_t uri_index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};
    /* index 的 URI 处理结构 */
    httpd_uri_t uri_senser = {
        .uri = "/senser",
        .method = HTTP_GET,
        .handler = senser_handler,
        .user_ctx = NULL};
    httpd_uri_t uri_config = {
        .uri = "/config",
        .method = HTTP_GET,
        .handler = config_handler,
        .user_ctx = NULL};
    httpd_uri_t uri_update = {
        .uri = "/update",
        .method = HTTP_GET,
        .handler = get_handler,
        .user_ctx = NULL};

    /* 生成默认的配置参数 */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* 启动 httpd server */
    if (httpd_start(&server, &config) == ESP_OK)
    {
        /* 注册 URI 处理程序 */
        httpd_register_uri_handler(server, &uri_index);
        httpd_register_uri_handler(server, &uri_config);
        httpd_register_uri_handler(server, &uri_senser);
        httpd_register_uri_handler(server, &uri_update);
    }
    ESP_LOGI(TAG, "start");
}

leida_web::~leida_web()
{
    if (server)
    {
        /* 停止 httpd server */
        httpd_stop(server);
        ESP_LOGI(TAG, "stop");
    }
}

esp_err_t leida_web::get_handler(httpd_req_t *req)
{
    /* 发送回简单的响应数据包 */
    const char resp[] = "URI GET Response";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "get_handler");
    return ESP_OK;
}

/* URI 处理函数，在客户端发起 POST/uri 请求时被调用 */
esp_err_t leida_web::index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)__index__, sizeof(__index__));
}
esp_err_t leida_web::senser_handler(httpd_req_t *req)
{
    const char resp[] = "URI GET Response";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "get_handler");
    return ESP_OK;
}
esp_err_t leida_web::config_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)__config__, sizeof(__config__));
}