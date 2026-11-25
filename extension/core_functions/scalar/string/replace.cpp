#include "core_functions/scalar/string_functions.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/common/vector_operations/ternary_executor.hpp"
#include "duckdb/function/scalar/string_common.hpp"

#include <string.h>
#include <ctype.h>
#include <unordered_map>

namespace duckdb {

static idx_t NextNeedle(const char *input_haystack, idx_t size_haystack, const char *input_needle,
                        const idx_t size_needle) {
	// Needle needs something to proceed
	if (size_needle > 0 && size_haystack > 0) {
		auto position = FindStrInStr(const_uchar_ptr_cast(input_haystack), size_haystack,
		                             const_uchar_ptr_cast(input_needle), size_needle);
		if (position != DConstants::INVALID_INDEX) {
			return position;
		}
	}
	// Did not find the needle
	return size_haystack;
}

static string_t ReplaceMatches(const string_t &haystack, const string_t &needle, const string_t &thread,
                               idx_t match_position, vector<char> &result) {
	// Get information about the needle, the haystack and the "thread"
	auto input_haystack = haystack.GetData();
	auto size_haystack = haystack.GetSize();

	auto input_needle = needle.GetData();
	auto size_needle = needle.GetSize();

	auto input_thread = thread.GetData();
	auto size_thread = thread.GetSize();

	//  Reuse the buffer
	result.clear();

	do {
		result.insert(result.end(), input_haystack, input_haystack + match_position);
		result.insert(result.end(), input_thread, input_thread + size_thread);
		input_haystack += match_position + size_needle;
		size_haystack -= match_position + size_needle;
		match_position = NextNeedle(input_haystack, size_haystack, input_needle, size_needle);
	} while (match_position < size_haystack);
	result.insert(result.end(), input_haystack, input_haystack + size_haystack);

	return string_t(result.data(), UnsafeNumericCast<uint32_t>(result.size()));
}

static void ReplaceFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	const auto &haystack_vector = args.data[0];
	const auto &needle_vector = args.data[1];
	const auto &thread_vector = args.data[2];

	vector<char> buffer;
	bool references_input_heap = false;
	auto &heap = StringVector::GetStringHeap(result);
	TernaryExecutor::Execute<string_t, string_t, string_t, string_t>(
	    haystack_vector, needle_vector, thread_vector, result,
	    [&](string_t input_string, string_t needle_string, string_t thread_string) {
		    auto match_position = NextNeedle(input_string.GetData(), input_string.GetSize(), needle_string.GetData(),
		                                     needle_string.GetSize());
		    if (match_position == input_string.GetSize()) {
			    references_input_heap |= !input_string.IsInlined();
			    return input_string;
		    }
		    return heap.AddString(ReplaceMatches(input_string, needle_string, thread_string, match_position, buffer));
	    });
	if (references_input_heap) {
		StringVector::AddHeapReference(result, haystack_vector);
	}
}

ScalarFunction ReplaceFun::GetFunction() {
	ScalarFunction fun({}, LogicalType::VARCHAR, ReplaceFunction);
	fun.GetSignature()
	    .AddParameter("string", LogicalType::VARCHAR)
	    .AddParameter("source", LogicalType::VARCHAR)
	    .AddParameter("target", LogicalType::VARCHAR);
	return fun;
}

} // namespace duckdb
