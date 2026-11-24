#ifndef LLM_ENGINE_H
#define LLM_ENGINE_H

#include <string>
#include <vector>
#include <functional>
#include <memory>

// Forward decl
struct llm_tokenizer;
typedef struct llm_tokenizer llm_tokenizer_t;

namespace hyper {

class LLMEngine {
public:
    LLMEngine();
    ~LLMEngine();

    // Initialize with model path (directory containing .onnx and tokenizer.json)
    bool LoadModel(const std::string& model_dir);
    
    // Generate shader code from prompt
    // returns the generated GLSL code
    std::string Generate(const std::string& prompt, int max_new_tokens = 512);

    bool IsLoaded() const { return m_loaded; }
    std::string GetLastError() const { return m_last_error; }

private:
    bool m_loaded = false;
    std::string m_last_error;
    std::string m_model_dir;

    llm_tokenizer_t* m_tokenizer = nullptr;

    // PImpl for ONNX Runtime types to avoid leaking headers
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hyper

#endif // LLM_ENGINE_H
