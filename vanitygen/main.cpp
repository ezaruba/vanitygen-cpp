#include "selftests.h"
#include "nemaddress.h"
#include "nemkey.h"
#include "utils.h"

#include "cppformat/format.h"
#include "leanmean/optionparser.h"
#include "pcg/pcg_basic.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <regex>
#include <string>

#include <memory.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>
#include <time.h>

// just to have fancy colors
#include <Windows.h>

#define info(strfmt, ...) do { fmt::print(" [.] "); fmt::print(strfmt, __VA_ARGS__); fmt::print("\n"); } while(0)

class Key
{
public:
	Key(uint8_t* key, bool reversed = false) :
		m_key(key),
		m_reversed(reversed)
	{ }

	friend std::ostream& operator<<(std::ostream &os, const Key &self) {
		fmt::MemoryWriter out;
		if (self.m_reversed) {
			for (int i = 31; i >= 0; --i) {
				out.write("{:02x}", self.m_key[i]);
			}
		} else {
			for (size_t i = 0; i < 32; ++i) {
				out.write("{:02x}", self.m_key[i]);
			}
		}
		
		return os << out.c_str();
	}
private:
	uint8_t* m_key;
	bool m_reversed;
};

void runGenerator(const std::string& needle) {
	uint64_t seed[2];
	pcg32_random_t _gen, *gen=&_gen;

	info("searching for: {}", needle);

	seed[0] = time(0);
	seed[1] = 0x696f3104;
	pcg32_srandom_r(gen, seed[0], seed[1]);
	for (int i = 0; i < 1000; ++i) pcg32_random_r(gen);

	uint8_t privateKey[32];
	uint8_t publicKey[32];
	char address[42];
	uint64_t c = 0;
	time_t start = time(0);
	bool printedStatusLine = false;

	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
	
	while (true)
	{
		fill(gen, (uint32_t*)privateKey, 32 / 4);
		crypto_sign_keypair(privateKey, publicKey);
		calculateAddress(publicKey, 32, address);
		c++;

		if (!(c % 1047)) {
			time_t end = time(0);
			fprintf(stdout, "\r%10lld keys % 8.2f keys per sec", c, c / (double)(end - start)); fflush(stdout);
			printedStatusLine = true;
		}

		const char* pos = strstr(address, needle.c_str());
		if (pos!= nullptr) {
			if (printedStatusLine) fmt::print("\n");
			// NOTE: we need to print the private key reversed to be compatible with NIS/NCC
			fmt::print("priv: {}", Key(privateKey, true));
			fmt::print("\npub : {}", Key(publicKey));
			printf("%.*s", pos-address, address);

			SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY | FOREGROUND_GREEN);
			printf("%.*s", needle.size(), pos);
			SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);

			printf("%s\n", pos + needle.size());
			printedStatusLine = false;
		}
	}
}

namespace detail
{
	class Line : public std::string
	{
		friend std::istream & operator>>(std::istream& is, Line& line)
		{
			return std::getline(is, line);
		}
	};
}

uint8_t strToVal(const char c) {
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= '0' && c <= '9') return c - '0';
	return 0;
}

uint8_t strToByte(const char* twoBytes) {
	return (strToVal(twoBytes[0]) << 4) | strToVal(twoBytes[1]);
}

// NOTE: this reverses the format of private key...
void inputStringToPrivateKey(const std::string& privString, uint8_t* privateKey) {
	if (privString.size() != 64) {
		throw std::runtime_error("private key in first column in input file must have 64 characters");
	}

	for (size_t i = 0; i < privString.size(); i += 2) {
		privateKey[31 - i / 2] = strToByte(&privString[i]);
	}
}

void inputStringToPublicKey(const std::string& pubString, uint8_t* publicKey) {
	if (pubString.size() != 64) {
		throw std::runtime_error("public key in third column in input file must have 64 characters");
	}

	for (size_t i = 0; i < pubString.size(); i += 2) {
		publicKey[i / 2] = strToByte(&pubString[i]);
	}
}

bool verifyLine(const std::string& line) {
	std::regex e("^: ([a-f0-9]+) : ([a-f0-9]+) : ([a-f0-9]+) : ([A-Z2-7]+)$");
	std::smatch sm;
	std::regex_match(line, sm, e);
	
	uint8_t privateKey[32];
	uint8_t computedPublicKey[32];
	uint8_t expectedPublicKey[32];
	char address[42];
	const std::string& expectedAddress = sm[4];

	inputStringToPrivateKey(sm[1], privateKey);
	inputStringToPublicKey(sm[3], expectedPublicKey);
	
	crypto_sign_keypair(privateKey, computedPublicKey);
	calculateAddress(computedPublicKey, 32, address);
	
	if (memcmp(expectedPublicKey, computedPublicKey, sizeof(computedPublicKey)) ||
		expectedAddress != address) {
		fmt::print("\nERROR\n");
		fmt::print("input private key: {}\n", sm[1]);
		fmt::print("      private key: {}\n", Key(privateKey));

		fmt::print("expected public key: {}\n", Key(expectedPublicKey));
		fmt::print("  actual public key: {}\n", Key(computedPublicKey));

		fmt::print("expected address: {}\n", expectedAddress);
		fmt::print("  actual address: {}\n", address);
		return false;
	}
	return true;
}

void runTestsOnFile(const std::string& filename) {
	typedef std::istream_iterator<detail::Line> LineIt;
	std::ifstream inputFile(filename);

	uint64_t c = 0;
	for (auto it = LineIt(inputFile), _it = LineIt(); it != _it; ++it) {
		if (!verifyLine(*it)) {
			return;
		}

		c++;

		if (!(c % 513)) {
			fmt::print("\r{:10d} tested keys", c);
		}
	}

	fmt::print("\n{:10d} TEST keys and addresses: OK!\n", c);
}

void printUsage()
{
	fmt::print(R"(
Usage: 
	vanitygen.exe <string-to-search>
)");
}

static option::ArgStatus argIsFile(const option::Option& opt, bool msg)
{
	using std::tr2::sys::exists;
	using std::tr2::sys::path;

	if (msg) {
		if (opt.arg == 0 || ::strlen(opt.arg) == 0 || !exists(path(opt.arg))) {
			fmt::print(" ERROR: cannot open file: {}", opt.arg);
			return option::ARG_ILLEGAL;
		}
	}

	return option::ArgStatus::ARG_OK;
}

enum  optionIndex { Unknown_Flag, Usage, Test_File, Skip_Self_Test };
const option::Descriptor usage[] =
{
	{ Unknown_Flag, 0, "", "", option::Arg::None, "USAGE: example [options]\n\nOptions:" },
	{ Usage, 0, "", "help", option::Arg::None, "  --help  \tPrint usage and exit." },
	{ Test_File, 0, "", "test-file", argIsFile, "  --test-file <file> \tConducts test on an intput file. " },
	{ Skip_Self_Test, 0, "", "skip-self-test", option::Arg::None, "  --skip-self-test  \tSkip self test." },
	{ Unknown_Flag, 0, "", "", option::Arg::None, R"(
EXAMPLES:
  vanitygen.exe foo
  vanitygen.exe --test-file testkeys.dat
  vanitygen.exe --skip-self-test bar
)" },
	{ 0, 0, 0, 0, 0, 0 }
};

static unsigned char base32[] = "234567ABCDEFGHIJKLMNOPQRSTUVWXYZ";

void usageHelper(const char* str, int size) {
	printf("%.*s", size, str);
}

int main(int argc, char** argv) {
	
	argc -= (argc > 0);
	argv += (argc > 0);
	
	option::Stats  stats(usage, argc, argv);

	option::Option *options = new option::Option[stats.options_max];
	option::Option *buffer = new option::Option[stats.buffer_max];
	option::Parser parse(usage, argc, argv, options, buffer);

	if (parse.error())
		return 1;

	if (options[Usage] || argc == 0) {
		option::printUsage(&usageHelper, usage);
		return 0;
	}

	if (options[Skip_Self_Test]) {
	} else if (!selfTest()) {
		return -3;
	}

	if (options[Test_File]) {
		runTestsOnFile(options[Test_File].arg);
		return 0;
	}

	if (parse.nonOptionsCount()) {
		std::string s = parse.nonOption(0);
		std::transform(s.begin(), s.end(), s.begin(), ::toupper);

		for (auto ch : s) {
			if (!std::binary_search(base32, base32 + _countof(base32), ch)) {
				fmt::print("Invalid character: {}, does not occur in base32", ch);
				return -2;
			}
		}

		runGenerator(s);
	}

	return 0;
}
