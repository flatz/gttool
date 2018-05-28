#include "volume.hpp"

#include <iostream>

#include <boost/program_options.hpp>

int main(int argc, const char* argv[])
{
	try {
		boost::program_options::options_description generalOpts("General options");
		generalOpts.add_options()
			("help,h", "Display help message")
			("unpack,u", "Unpack volume files")
			("decrypt,d", "Decrypt file")
		;

		boost::program_options::options_description unpackOpts("Unpack options");
		unpackOpts.add_options()
			("input,i", boost::program_options::value<std::string>(), "Volume/Index file")
			("output,o", boost::program_options::value<std::string>(), "Output directory")
		;

		boost::program_options::options_description decryptOpts("Decrypt options");
		decryptOpts.add_options()
			("input,i", boost::program_options::value<std::string>(), "Input file")
			("output,o", boost::program_options::value<std::string>(), "Output file")
			("key,k", boost::program_options::value<std::string>(), "Encryption key")
		;

		boost::program_options::options_description allOpts;
		allOpts.add(generalOpts).add(unpackOpts).add(decryptOpts);

		auto parsedOpts = boost::program_options::command_line_parser(argc, argv)
			.style(boost::program_options::command_line_style::unix_style)
			.allow_unregistered()
			.options(generalOpts)
			.run();

		boost::program_options::variables_map varMap;
		boost::program_options::store(parsedOpts, varMap);
		const auto restParams = boost::program_options::collect_unrecognized(parsedOpts.options, boost::program_options::include_positional);
		boost::program_options::notify(varMap);

		if (varMap.count("help")) {
show_help:
			std::cout << "GT Tool (c) flatz, 2018" << std::endl;
			std::cout << allOpts;
		} else if (varMap.count("decrypt")) {
			boost::program_options::variables_map restVarMap;
			boost::program_options::store(
				boost::program_options::command_line_parser(restParams)
					.style(boost::program_options::command_line_style::unix_style)
					.allow_unregistered()
					.options(decryptOpts)
					.run(),
				restVarMap
			);
			boost::program_options::notify(restVarMap);

			if (!restVarMap.count("input") || !restVarMap.count("output") || !restVarMap.count("key")) {
				goto show_help;
			}

			const auto& inFile = restVarMap["input"].as<std::string>();
			const auto& outFile = restVarMap["output"].as<std::string>();
			const auto& keyStr = restVarMap["key"].as<std::string>();

			uint8_t key[0x20] = {};
			if (parseHexString(keyStr, key, sizeof(key)) != sizeof(key)) {
				std::cerr << "Invalid key specified." << std::endl;
				return EXIT_FAILURE;
			}

			if (!boost::filesystem::exists(inFile) || !boost::filesystem::is_regular_file(inFile)) {
				std::cerr << "Invalid input file specified." << std::endl;
				return EXIT_FAILURE;
			}
			if (boost::filesystem::exists(outFile) && !boost::filesystem::is_regular_file(outFile)) {
				std::cerr << "Invalid output file specified." << std::endl;
				return EXIT_FAILURE;
			}

			std::vector<uint8_t> data;
			if (!loadFromFile(inFile, data)) {
				std::cerr << "Unable to load input file." << std::endl;
				return EXIT_FAILURE;
			}

			std::cout << "Decrypting file..." << std::endl;

			Salsa20Cipher cipher(key, sizeof(key));
			cipher.processBytes(data.data(), data.data(), data.size());

			if (!saveToFile(outFile, data.data(), data.size())) {
				std::cerr << "Unable to load input file." << std::endl;
				return EXIT_FAILURE;
			}

			std::cout << "Done!" << std::endl;
			return EXIT_SUCCESS;
		} else if (varMap.count("unpack")) {
			boost::program_options::variables_map restVarMap;
			boost::program_options::store(
				boost::program_options::command_line_parser(restParams)
					.style(boost::program_options::command_line_style::unix_style)
					.allow_unregistered()
					.options(decryptOpts)
					.run(),
				restVarMap
			);
			boost::program_options::notify(restVarMap);

			if (!restVarMap.count("input") || !restVarMap.count("output")) {
				goto show_help;
			}

			const auto& inFile = restVarMap["input"].as<std::string>();
			const auto& outDir = restVarMap["output"].as<std::string>();

			if (!boost::filesystem::exists(inFile) || !boost::filesystem::is_regular_file(inFile)) {
				std::cerr << "Invalid volume file specified." << std::endl;
				return EXIT_FAILURE;
			}
			if (boost::filesystem::exists(outDir) && !boost::filesystem::is_directory(outDir)) {
				std::cerr << "Invalid output directory specified." << std::endl;
				return EXIT_FAILURE;
			}

			GT5VolumeFile vol5;
			GT6VolumeFile vol6;
			GT7VolumeFile vol7;
			std::array<VolumeFile*, 3> volumes = {{ &vol5, &vol6, &vol7 }};
			VolumeFile* volume = nullptr;
			for (auto vol: volumes) {
				if (vol->load(inFile)) {
					volume = vol;
					break;
				}
			}
			if (!volume) {
				std::cerr << "Unable to load volume file." << std::endl;
				return EXIT_FAILURE;
			}

			std::cout << "Unpacking files..." << std::endl;
			if (!volume->unpackAll(outDir)) {
				std::cerr << "Unable to unpack volume file." << std::endl;
				return EXIT_FAILURE;
			}

			std::cout << "Done!" << std::endl;
			return EXIT_SUCCESS;
		} else {
			goto show_help;
		}
	}
	catch (const std::exception& e) {
		std::cerr << "Unhandled error occurred:" << std::endl << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return 0;
}