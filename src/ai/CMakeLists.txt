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
target_link_libraries(af_ai PUBLIC af_common)
