#include <filesystem>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <string>
#include <string_view>
#include <regex>
#include <cassert>

std::string std_bytenise(std::string_view binary_data)
{
	std::string res;
	for(std::size_t i = 0; i < binary_data.size(); i++)
	{
		res += std::to_string(static_cast<std::int8_t>(binary_data[i]));
		if(i != binary_data.size() - 1)
		{
			res += ",";
		}
	}
	return res;
}

namespace textc
{
	namespace detail
	{
		using SourceMatchIteratorType = std::vector<std::string>::iterator;
		template<typename F, typename Result, typename... Args>
		concept Function = requires(F f, Args... args)
		{
			{f(args...)} -> std::convertible_to<Result>;
		};

		template<typename... Args>
		void assert_internal([[maybe_unused]] bool eval, [[maybe_unused]] const char* fmt, [[maybe_unused]] Args&&... args)
		{
			#ifndef NDEBUG
				if(!eval)
				{
					std::fflush(stderr);
					std::fprintf(stderr, fmt, std::forward<Args>(args)...);
					std::fflush(stderr);
					assert(eval);
				}
			#endif
		}
	}

	#ifdef textc_assert
	#undef textc_assert
	#endif

	/**
	 * @ingroup tz_core
	 * @fn textc_assert(eval, msg, ...)
	 * @hideinitializer
	 * Debug-only assert on some condition. If the condition evaluates to false, a runtime error is raised similar to @ref tz_error
	 */
	#define textc_assert(eval, msg, ...) {textc::detail::assert_internal(eval, "textc_assert Failure: %s\nIn file: %s:%d:\n\t " msg "\n", #eval, __FILE__, __LINE__, ##__VA_ARGS__);} (void)0;



	template<typename F>
	concept SourceReplaceAction = detail::Function<F, std::string, detail::SourceMatchIteratorType, detail::SourceMatchIteratorType>;
	bool transform(std::string& source, std::regex reg, SourceReplaceAction auto transform_function)
	{
		using ReplaceJob = std::pair<std::pair<std::size_t, std::size_t>, std::string>;
		std::vector<ReplaceJob> replacements;
		
		std::string src_copy = source;
		std::smatch sm;
		std::size_t src_pos_counter = 0;

		bool did_any_work = false;

		while(std::regex_search(src_copy, sm, reg))
		{
			// Found a match!
			std::size_t pos = sm.position() + src_pos_counter;
			textc_assert(pos < source.size(), "Match result has position %zu in source which is out of range. Size: %zu", pos, source.size());
			std::size_t len = sm.length();
			std::vector<std::string> inner_matches;
			for(std::size_t i = 1; i < sm.size(); i++)
				inner_matches.push_back(sm[i]);
			src_copy = sm.suffix();
			src_pos_counter = source.find(src_copy);
			//src_pos_counter += pos + len;
			// get the transformed string.
			std::string replacement = transform_function(inner_matches.begin(), inner_matches.end());
			// register this source replacement.
			ReplaceJob job{{pos, len}, replacement};
			replacements.push_back(job);
		}

		did_any_work = !replacements.empty();

		// Invoke all replacements (in reverse-order)
		for(auto i = replacements.rbegin(); i != replacements.rend(); i++)
		{
			std::string replacement = i->second;
			std::size_t pos = i->first.first;
			std::size_t len = i->first.second;
			source.replace(pos, len, replacement);
		}

		return did_any_work;
	}
}

void headerify(std::string_view filename, std::string& text)
{
	std::string filename_cpy{filename};
	// filename is a full path e.g "root/path_parent/filename.txt"
	// we want to get filename_txt.
	const char* regex_str =  ".*[\\/\\\\]([a-zA-Z_0-9]+)\\.([a-zA-Z]+)";
	textc::transform(filename_cpy, std::regex{regex_str}, [&](auto beg, auto end)->std::string
	{
		std::string part_we_want = *beg;
		std::string extension = *(beg + 1);
		return part_we_want + "_" + extension;
	});
	// If we have multiple file extensions, there will be dots sitting in the variable name. Let's change these all to underscores.
	filename_cpy = std::regex_replace(filename_cpy, std::regex{"\\."}, "_");

	std::size_t byte_count = text.size();
	std::string buffer_array_literal = std_bytenise(text);
	text = std::string("#include <cstdint>\n#include <array>\n/*textc_gen_header*/constexpr std::array<std::int8_t, " + std::to_string(byte_count) + "> " + filename_cpy + "{") + buffer_array_literal + "};";
}

FILE* get_output_stream(int argc, char** argv)
{
	// We're either writing to stdout or a specific file. Return the relevant filestream
	FILE* output = stdout;
	for(std::size_t i = 0; i < argc - 1; i++)
	{
		std::string_view arg{argv[i]};
		std::string_view arg_next{argv[i + 1]};
		if(arg == "-o")
		{
			std::filesystem::path out_path{arg_next.data()};
			std::filesystem::path parent = out_path.parent_path();
			if(!std::filesystem::exists(parent))
			{
				std::filesystem::create_directories(parent);
			}
			output = fopen(arg_next.data(), "w");
			textc_assert(output != nullptr, "Failed to open output stream. Perhaps missing intermediate directory, or no write permissions for this area of the filesystem?\nOutput was %s.\nErrno says: %s", arg_next.data(), std::strerror(errno));
		}
	}
	return output;
}

int main(int argc, char** argv)
{
	textc_assert(argc >= 2, "Not enough arguments (%d). At least 2. Usage: `textc <file-path> [-o <output_file_path>]`", argc);
	const char* txt_filename = argv[1];
	FILE* out = get_output_stream(argc, argv);

	std::ifstream file{txt_filename, std::ios::ate | std::ios::binary};
	textc_assert(file.is_open(), "Cannot open text file %s", txt_filename);

	auto file_size_bytes = static_cast<std::size_t>(file.tellg());
	file.seekg(0);
	std::string buffer;
	buffer.resize(file_size_bytes);
	file.read(buffer.data(), file_size_bytes);
	file.close();

	headerify(txt_filename, buffer);
	for(char c : buffer)
	{
		std::fprintf(out, "%c", c);
	}
	if(out != stdout)
	{
		fclose(out);
	}
}
