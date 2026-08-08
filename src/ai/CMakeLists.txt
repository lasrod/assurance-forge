add_library(af_ai OBJECT
    ai_claim_review.cpp
    ai_service.cpp
    ai_settings.cpp
    ai_task_runner.cpp
    ai_types.cpp
    http_client.cpp
    libcurl_http_client.cpp
    openai_provider.cpp
    secret_store.cpp
)
# libcurl is reached through one file, libcurl_http_client.cpp. Keeping it
# private is what stops a second HTTP path appearing elsewhere by accident.
target_link_libraries(af_ai PUBLIC af_common)
target_link_libraries(af_ai PRIVATE CURL::libcurl)
