#include "llm_engine.h"
#include "tokenizer.h"
#include <obs-module.h>

#ifdef USE_LITERT
#include <litert/c/litert_common.h>
#include <litert/c/litert_compiled_model.h>
#include <litert/c/litert_environment.h>
#include <litert/c/litert_model.h>
#include <litert/c/litert_tensor_buffer.h>
#include <litert/c/litert_tensor_buffer_requirements.h>
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace hyper {

#ifdef USE_LITERT

namespace {

// Qwen2.5-0.5B-Instruct (seq128 / ekv1280) graph constants
constexpr size_t kMaxSeqLen = 128;   // prefill_128 fixed context
constexpr size_t kKvTensors = 48;    // 24 layers * (k, v)
constexpr size_t kKvFloats = 1280 * 2 * 64; // per kv cache tensor

bool write_buffer(LiteRtTensorBuffer buf, const void *data, size_t bytes)
{
	void *host = nullptr;
	if (LiteRtLockTensorBuffer(buf, &host, kLiteRtTensorBufferLockModeWrite) !=
	    kLiteRtStatusOk)
		return false;
	memcpy(host, data, bytes);
	return LiteRtUnlockTensorBuffer(buf) == kLiteRtStatusOk;
}

bool read_buffer(LiteRtTensorBuffer buf, void *data, size_t bytes)
{
	void *host = nullptr;
	if (LiteRtLockTensorBuffer(buf, &host, kLiteRtTensorBufferLockModeRead) !=
	    kLiteRtStatusOk)
		return false;
	memcpy(data, host, bytes);
	return LiteRtUnlockTensorBuffer(buf) == kLiteRtStatusOk;
}

// Introspects one signature, creates the managed host-memory buffers in
// signature I/O order (the order LiteRtRunCompiledModel expects), and records
// which input/output slots are the token, position, KV-cache and logits
// tensors. The model's tensor order is scrambled, so everything is name-based.
bool setup_signature(LiteRtEnvironment env, LiteRtCompiledModel compiled,
		     LiteRtSignature sig, LiteRtParamIndex sig_idx,
		     std::vector<LiteRtTensorBuffer> &inputs,
		     std::vector<LiteRtTensorBuffer> &outputs,
		     size_t &tokens_idx, size_t &pos_idx, size_t &logits_idx,
		     std::vector<size_t> &kv_in_idx, std::vector<size_t> &kv_out_idx,
		     size_t &vocab_out, std::string &err)
{
	LiteRtParamIndex num_in = 0, num_out = 0;
	if (LiteRtGetNumSignatureInputs(sig, &num_in) != kLiteRtStatusOk) {
		err = "LiteRtGetNumSignatureInputs failed";
		return false;
	}
	if (LiteRtGetNumSignatureOutputs(sig, &num_out) != kLiteRtStatusOk) {
		err = "LiteRtGetNumSignatureOutputs failed";
		return false;
	}

	inputs.resize(num_in);
	outputs.resize(num_out);
	kv_in_idx.clear();
	kv_out_idx.clear();
	tokens_idx = 0;
	pos_idx = 0;
	logits_idx = 0;

	for (LiteRtParamIndex i = 0; i < num_in; ++i) {
		const char *name = nullptr;
		if (LiteRtGetSignatureInputName(sig, i, &name) != kLiteRtStatusOk) {
			err = "LiteRtGetSignatureInputName failed";
			return false;
		}
		const std::string_view n(name ? name : "");
		if (n.find("kv_cache") != std::string_view::npos)
			kv_in_idx.push_back(i);
		else if (n.find("tokens") != std::string_view::npos)
			tokens_idx = i;
		else if (n.find("input_pos") != std::string_view::npos)
			pos_idx = i;

		LiteRtTensor tensor = nullptr;
		if (LiteRtGetSignatureInputTensorByIndex(sig, i, &tensor) !=
		    kLiteRtStatusOk) {
			err = "LiteRtGetSignatureInputTensorByIndex failed";
			return false;
		}
		LiteRtRankedTensorType rt = {};
		if (LiteRtGetRankedTensorType(tensor, &rt) != kLiteRtStatusOk) {
			err = "LiteRtGetRankedTensorType failed";
			return false;
		}
		LiteRtTensorBufferRequirements req = nullptr;
		if (LiteRtGetCompiledModelInputBufferRequirements(
			    compiled, sig_idx, i, &req) != kLiteRtStatusOk) {
			err = "LiteRtGetCompiledModelInputBufferRequirements failed";
			return false;
		}
		LiteRtTensorBuffer buf = nullptr;
		if (LiteRtCreateManagedTensorBufferFromRequirements(
			    env, &rt, req, &buf) != kLiteRtStatusOk) {
			err = "LiteRtCreateManagedTensorBufferFromRequirements failed";
			return false;
		}
		inputs[i] = buf;
	}

	for (LiteRtParamIndex i = 0; i < num_out; ++i) {
		LiteRtTensor tensor = nullptr;
		if (LiteRtGetSignatureOutputTensorByIndex(sig, i, &tensor) !=
		    kLiteRtStatusOk) {
			err = "LiteRtGetSignatureOutputTensorByIndex failed";
			return false;
		}
		LiteRtRankedTensorType rt = {};
		if (LiteRtGetRankedTensorType(tensor, &rt) != kLiteRtStatusOk) {
			err = "LiteRtGetRankedTensorType failed";
			return false;
		}
		LiteRtTensorBufferRequirements req = nullptr;
		if (LiteRtGetCompiledModelOutputBufferRequirements(
			    compiled, sig_idx, i, &req) != kLiteRtStatusOk) {
			err = "LiteRtGetCompiledModelOutputBufferRequirements failed";
			return false;
		}
		LiteRtTensorBuffer buf = nullptr;
		if (LiteRtCreateManagedTensorBufferFromRequirements(
			    env, &rt, req, &buf) != kLiteRtStatusOk) {
			err = "LiteRtCreateManagedTensorBufferFromRequirements failed";
			return false;
		}
		outputs[i] = buf;

		size_t bytes = 0;
		if (LiteRtGetTensorBufferPackedSize(buf, &bytes) != kLiteRtStatusOk) {
			err = "LiteRtGetTensorBufferPackedSize failed";
			return false;
		}
		if (bytes / sizeof(float) == kKvFloats)
			kv_out_idx.push_back(i);
		else
			logits_idx = i;
	}

	if (kv_in_idx.size() != kKvTensors || kv_out_idx.size() != kKvTensors) {
		err = "expected " + std::to_string(kKvTensors) +
		      " kv tensors, got " + std::to_string(kv_in_idx.size()) + " in / " +
		      std::to_string(kv_out_idx.size()) + " out";
		return false;
	}

	size_t logits_bytes = 0;
	if (LiteRtGetTensorBufferPackedSize(outputs[logits_idx], &logits_bytes) !=
	    kLiteRtStatusOk) {
		err = "logits PackedSize failed";
		return false;
	}
	if (logits_bytes % sizeof(float) != 0) {
		err = "invalid logits tensor";
		return false;
	}
	vocab_out = logits_bytes / sizeof(float);
	if (vocab_out == 0) {
		err = "empty logits tensor";
		return false;
	}

	return true;
}

int32_t argmax_row(const float *logits, size_t vocab)
{
	size_t best = 0;
	float best_val = logits[0];
	for (size_t i = 1; i < vocab; ++i) {
		if (logits[i] > best_val) {
			best_val = logits[i];
			best = i;
		}
	}
	return (int32_t)best;
}

} // namespace

// LiteRT CompiledModel implementation details (raw C API against libLiteRt.so)
struct LLMEngine::Impl {
	LiteRtEnvironment env = nullptr;
	LiteRtModel model = nullptr;
	LiteRtCompiledModel compiled = nullptr;

	LiteRtParamIndex prefill_sig = 0;
	LiteRtParamIndex decode_sig = 0;

	// Buffers (owned), in signature I/O order as expected by LiteRtRunCompiledModel
	std::vector<LiteRtTensorBuffer> prefill_inputs;
	std::vector<LiteRtTensorBuffer> prefill_outputs;
	std::vector<LiteRtTensorBuffer> decode_inputs;
	std::vector<LiteRtTensorBuffer> decode_outputs;

	size_t pre_tokens_idx = 0, pre_pos_idx = 0, pre_logits_idx = 0;
	size_t dec_tokens_idx = 0, dec_pos_idx = 0, dec_logits_idx = 0;
	std::vector<size_t> pre_kv_in_idx, pre_kv_out_idx;
	std::vector<size_t> dec_kv_in_idx, dec_kv_out_idx;
	size_t vocab_size = 0;

	std::vector<float> kv_cache; // kKvTensors * kKvFloats, contiguous
	std::vector<int32_t> prefill_tokens; // kMaxSeqLen
	std::vector<int32_t> prefill_pos;    // kMaxSeqLen (0..127)
	std::vector<float> prefill_logits;   // kMaxSeqLen * vocab
	std::vector<int32_t> decode_tokens;  // 1
	std::vector<int32_t> decode_pos;     // 1
	std::vector<float> decode_logits;    // vocab

	void release()
	{
		auto destroy = [](std::vector<LiteRtTensorBuffer> &v) {
			for (LiteRtTensorBuffer b : v)
				if (b)
					LiteRtDestroyTensorBuffer(b);
			v.clear();
		};
		destroy(prefill_inputs);
		destroy(prefill_outputs);
		destroy(decode_inputs);
		destroy(decode_outputs);
		if (compiled)
			LiteRtDestroyCompiledModel(compiled);
		if (model)
			LiteRtDestroyModel(model);
		if (env)
			LiteRtDestroyEnvironment(env);
		compiled = nullptr;
		model = nullptr;
		env = nullptr;
	}
};
#else
// Fallback for builds without LiteRT
struct LLMEngine::Impl {};
#endif

LLMEngine::LLMEngine() : m_impl(new Impl()) {}

LLMEngine::~LLMEngine()
{
	if (m_tokenizer) {
		llm_tokenizer_destroy(m_tokenizer);
	}
#ifdef USE_LITERT
	if (m_impl)
		m_impl->release();
#endif
}

bool LLMEngine::LoadModel(const std::string &model_dir)
{
	m_model_dir = model_dir;
	m_last_error.clear();
	m_loaded = false;

	// Destroy existing tokenizer if any (for retry scenarios)
	if (m_tokenizer) {
		llm_tokenizer_destroy(m_tokenizer);
		m_tokenizer = nullptr;
	}

	// Load Tokenizer
	std::string tokenizer_path = model_dir + "/tokenizer.json";
	m_tokenizer = llm_tokenizer_create(tokenizer_path.c_str());
	if (!m_tokenizer) {
		m_last_error = "Failed to load tokenizer from " + tokenizer_path;
		return false;
	}

#ifdef USE_LITERT
	auto fail = [this](const std::string &msg) {
		m_last_error = msg;
		if (m_impl)
			m_impl->release();
		if (m_tokenizer) {
			llm_tokenizer_destroy(m_tokenizer);
			m_tokenizer = nullptr;
		}
		return false;
	};

	std::string model_path = model_dir + "/model.tflite";
	std::ifstream f(model_path.c_str());
	if (!f.good())
		return fail("Model file not found: " + model_path);

	if (LiteRtCreateEnvironment(0, nullptr, &m_impl->env) != kLiteRtStatusOk)
		return fail("LiteRtCreateEnvironment failed");

	if (LiteRtCreateModelFromFile(m_impl->env, model_path.c_str(),
				      &m_impl->model) != kLiteRtStatusOk)
		return fail("Failed to load model: " + model_path);

	// Locate the prefill_128 and decode signatures by key
	LiteRtParamIndex num_sigs = 0;
	if (LiteRtGetNumModelSignatures(m_impl->model, &num_sigs) != kLiteRtStatusOk)
		return fail("Failed to read model signatures");

	bool found_pre = false, found_dec = false;
	for (LiteRtParamIndex si = 0; si < num_sigs; ++si) {
		LiteRtSignature sig = nullptr;
		if (LiteRtGetModelSignature(m_impl->model, si, &sig) != kLiteRtStatusOk)
			return fail("Failed to read model signature");
		const char *key = nullptr;
		if (LiteRtGetSignatureKey(sig, &key) != kLiteRtStatusOk)
			return fail("Failed to read signature key");
		if (key && strcmp(key, "prefill_128") == 0) {
			m_impl->prefill_sig = si;
			found_pre = true;
		} else if (key && strcmp(key, "decode") == 0) {
			m_impl->decode_sig = si;
			found_dec = true;
		}
	}
	if (!found_pre)
		return fail("prefill_128 signature not found in model");
	if (!found_dec)
		return fail("decode signature not found in model");

	if (LiteRtCreateCompiledModel(m_impl->env, m_impl->model, nullptr,
				      &m_impl->compiled) != kLiteRtStatusOk)
		return fail("Failed to compile model");

	std::string err;
	LiteRtSignature pre_sig = nullptr, dec_sig = nullptr;
	LiteRtGetModelSignature(m_impl->model, m_impl->prefill_sig, &pre_sig);
	LiteRtGetModelSignature(m_impl->model, m_impl->decode_sig, &dec_sig);

	if (!setup_signature(m_impl->env, m_impl->compiled, pre_sig,
			     m_impl->prefill_sig, m_impl->prefill_inputs,
			     m_impl->prefill_outputs, m_impl->pre_tokens_idx,
			     m_impl->pre_pos_idx, m_impl->pre_logits_idx,
			     m_impl->pre_kv_in_idx, m_impl->pre_kv_out_idx,
			     m_impl->vocab_size, err))
		return fail(err);
	if (!setup_signature(m_impl->env, m_impl->compiled, dec_sig,
			     m_impl->decode_sig, m_impl->decode_inputs,
			     m_impl->decode_outputs, m_impl->dec_tokens_idx,
			     m_impl->dec_pos_idx, m_impl->dec_logits_idx,
			     m_impl->dec_kv_in_idx, m_impl->dec_kv_out_idx,
			     m_impl->vocab_size, err))
		return fail(err);

	m_impl->kv_cache.assign(kKvTensors * kKvFloats, 0.0f);
	m_impl->prefill_tokens.assign(kMaxSeqLen, 0);
	m_impl->prefill_pos.resize(kMaxSeqLen);
	for (size_t i = 0; i < kMaxSeqLen; ++i)
		m_impl->prefill_pos[i] = (int32_t)i;
	m_impl->prefill_logits.resize(kMaxSeqLen * m_impl->vocab_size);
	m_impl->decode_tokens.resize(1);
	m_impl->decode_pos.resize(1);
	m_impl->decode_logits.resize(m_impl->vocab_size);

	m_loaded = true;
#else
	m_last_error = "LiteRT not enabled in build.";
	// If tokenizer was loaded, destroy it for a clean state
	if (m_tokenizer) {
		llm_tokenizer_destroy(m_tokenizer);
		m_tokenizer = nullptr;
	}
	return false;
#endif

	return m_loaded;
}

std::string LLMEngine::Generate(const std::string &prompt, int max_new_tokens)
{
	if (!m_loaded || !m_tokenizer)
		return "";

	/* When building without LiteRT, the implementation below is compiled out. */
	(void)prompt;
	(void)max_new_tokens;

#ifdef USE_LITERT
	const int32_t eos_id = 151643; // Qwen2.5 <|endoftext|>

	// 1. Tokenize Prompt (Qwen BPE, truncated to the fixed 128-token context)
	std::vector<int32_t> prompt_ids(kMaxSeqLen);
	size_t prompt_len =
		llm_tokenize(m_tokenizer, prompt.c_str(), prompt_ids.data(), kMaxSeqLen);
	if (prompt_len == 0) {
		m_last_error = "Failed to tokenize prompt";
		return "";
	}

	auto set_error = [this](const std::string &msg) {
		m_last_error = msg;
	};

	auto *impl = m_impl.get();

	// 2. PREFILL: run the full 128-token context once with a zero KV cache
	for (size_t i = 0; i < kMaxSeqLen; ++i)
		impl->prefill_tokens[i] = (i < prompt_len) ? prompt_ids[i] : eos_id;

	std::fill(impl->kv_cache.begin(), impl->kv_cache.end(), 0.0f);
	for (size_t j = 0; j < kKvTensors; ++j) {
		if (!write_buffer(impl->prefill_inputs[impl->pre_kv_in_idx[j]],
				  impl->kv_cache.data() + j * kKvFloats,
				  kKvFloats * sizeof(float))) {
			set_error("Failed to write prefill KV input");
			return "";
		}
	}
	if (!write_buffer(impl->prefill_inputs[impl->pre_tokens_idx],
			  impl->prefill_tokens.data(),
			  impl->prefill_tokens.size() * sizeof(int32_t))) {
		set_error("Failed to write prefill tokens");
		return "";
	}
	if (!write_buffer(impl->prefill_inputs[impl->pre_pos_idx],
			  impl->prefill_pos.data(),
			  impl->prefill_pos.size() * sizeof(int32_t))) {
		set_error("Failed to write prefill positions");
		return "";
	}

	if (LiteRtRunCompiledModel(impl->compiled, impl->prefill_sig,
				   impl->prefill_inputs.size(),
				   impl->prefill_inputs.data(),
				   impl->prefill_outputs.size(),
				   impl->prefill_outputs.data()) != kLiteRtStatusOk) {
		set_error("prefill run failed");
		return "";
	}

	for (size_t j = 0; j < kKvTensors; ++j) {
		if (!read_buffer(impl->prefill_outputs[impl->pre_kv_out_idx[j]],
				 impl->kv_cache.data() + j * kKvFloats,
				 kKvFloats * sizeof(float))) {
			set_error("Failed to read prefill KV output");
			return "";
		}
	}
	if (!read_buffer(impl->prefill_outputs[impl->pre_logits_idx],
			  impl->prefill_logits.data(),
			  impl->prefill_logits.size() * sizeof(float))) {
		set_error("Failed to read prefill logits");
		return "";
	}

	// Greedy sample the token after the last real prompt position
	int32_t next_token = argmax_row(
		impl->prefill_logits.data() + (prompt_len - 1) * impl->vocab_size,
		impl->vocab_size);

	// 3. DECODE LOOP: one token per step, KV cache carried forward
	std::vector<int32_t> generated;
	int32_t pos = (int32_t)prompt_len;

	for (int step = 0; step < max_new_tokens; step++) {
		if (next_token == eos_id)
			break;

		generated.push_back(next_token);

		impl->decode_tokens[0] = next_token;
		impl->decode_pos[0] = pos;

		for (size_t j = 0; j < kKvTensors; ++j) {
			if (!write_buffer(impl->decode_inputs[impl->dec_kv_in_idx[j]],
					  impl->kv_cache.data() + j * kKvFloats,
					  kKvFloats * sizeof(float))) {
				set_error("Failed to write decode KV input");
				goto decode_done;
			}
		}
		if (!write_buffer(impl->decode_inputs[impl->dec_tokens_idx],
				  impl->decode_tokens.data(),
				  impl->decode_tokens.size() * sizeof(int32_t))) {
			set_error("Failed to write decode tokens");
			goto decode_done;
		}
		if (!write_buffer(impl->decode_inputs[impl->dec_pos_idx],
				  impl->decode_pos.data(),
				  impl->decode_pos.size() * sizeof(int32_t))) {
			set_error("Failed to write decode position");
			goto decode_done;
		}

		if (LiteRtRunCompiledModel(impl->compiled, impl->decode_sig,
					   impl->decode_inputs.size(),
					   impl->decode_inputs.data(),
					   impl->decode_outputs.size(),
					   impl->decode_outputs.data()) !=
		    kLiteRtStatusOk) {
			set_error("decode run failed");
			goto decode_done;
		}

		for (size_t j = 0; j < kKvTensors; ++j) {
			if (!read_buffer(impl->decode_outputs[impl->dec_kv_out_idx[j]],
					  impl->kv_cache.data() + j * kKvFloats,
					  kKvFloats * sizeof(float))) {
				set_error("Failed to read decode KV output");
				goto decode_done;
			}
		}
		if (!read_buffer(impl->decode_outputs[impl->dec_logits_idx],
				  impl->decode_logits.data(),
				  impl->decode_logits.size() * sizeof(float))) {
			set_error("Failed to read decode logits");
			goto decode_done;
		}

		next_token = argmax_row(impl->decode_logits.data(), impl->vocab_size);
		pos++;
	}
decode_done:

	if (!m_last_error.empty() && generated.empty())
		return "";

	// 4. Decode result
	std::vector<char> out_buf(8192); // Large buffer for shader code
	llm_decode(m_tokenizer, generated.data(), generated.size(), out_buf.data(),
		   out_buf.size());

	return std::string(out_buf.data());
#endif
	return "Error: LiteRT not enabled in build.";
}

} // namespace hyper

#include "llm_c_api.h"
#include <cstring>

extern "C" {

struct llm_instance {
	hyper::LLMEngine *engine;
	std::string err_cache;
};

llm_instance_t *llm_create_instance(void)
{
	llm_instance_t *i = new llm_instance_t;
	i->engine = new hyper::LLMEngine();
	return i;
}

void llm_destroy_instance(llm_instance_t *inst)
{
	if (inst) {
		delete inst->engine;
		delete inst;
	}
}

int llm_load_model(llm_instance_t *inst, const char *model_dir)
{
	if (inst && inst->engine && model_dir) {
		return inst->engine->LoadModel(model_dir) ? 1 : 0;
	}
	return 0;
}

char *llm_generate(llm_instance_t *inst, const char *prompt)
{
	if (inst && inst->engine && prompt) {
		std::string res = inst->engine->Generate(prompt);
		// Use OBS bstrdup which uses bmalloc
		return bstrdup(res.c_str());
	}
	return NULL;
}

void llm_free(char *str)
{
	if (str)
		bfree(str);
}

const char *llm_get_error(llm_instance_t *inst)
{
	if (inst && inst->engine) {
		inst->err_cache = inst->engine->GetLastError();
		return inst->err_cache.c_str();
	}
	return "";
}

} // extern C
