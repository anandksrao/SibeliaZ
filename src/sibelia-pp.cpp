#include <tclap/CmdLine.h>

#include "lightpath.h"

size_t Atoi(const char * str)
{
	size_t ret;
	std::stringstream ss(str);
	ss >> ret;
	return ret;
}

//const int64_t Sibelia::Assignment::UNKNOWN_BLOCK = INT32_MAX;

class OddConstraint : public TCLAP::Constraint < unsigned int >
{
public:
	~OddConstraint()
	{

	}

	std::string description() const
	{
		return "value of K must be odd";
	}

	std::string shortID() const
	{
		return "oddc";
	}

	bool check(const unsigned & value) const
	{
		return (value % 2) == 1;
	}
};

int main(int argc, char * argv[])
{
	OddConstraint constraint;

	try
	{
		TCLAP::CmdLine cmd("Program for construction of synteny blocks from complete genomes", ' ', "0.0.1");

		TCLAP::ValueArg<unsigned int> kvalue("k",
			"kvalue",
			"Value of k",
			false,
			25,
			&constraint,
			cmd);

		TCLAP::ValueArg<unsigned int> maxBranchSize("b",
			"branchsize",
			"Maximum branch size",
			false,
			125,
			"integer",
			cmd);

		TCLAP::ValueArg<unsigned int> maxFlankingSize("f",
			"flanksize",
			"Maximum flank size",
			false,
			50,
			"integer",
			cmd);

		TCLAP::ValueArg<unsigned int> minBlockSize("m",
			"blocksize",
			"Minimum block size",
			false,
			300,
			"integer",
			cmd);

		TCLAP::ValueArg<unsigned int> lookingDepth("",
			"depth",
			"Looking depth",
			false,
			8,
			"integer",
			cmd);

		TCLAP::ValueArg<unsigned int> threads("t",
			"threads",
			"Number of worker threads",
			false,
			1,
			"integer",
			cmd);

		TCLAP::ValueArg<unsigned int> sampleSize("",
			"ssize",
			"Sample size for randomized walk",
			false,
			0,
			"integer",
			cmd);

		TCLAP::ValueArg<std::string> tmpDirName("",
			"tmpdir",
			"Temporary directory name",
			false,
			".",
			"directory name",
			cmd);	

		TCLAP::ValueArg<std::string> inFileName("",
			"infile",
			"Input file name",
			true,
			"de_bruijn.bin",
			"file name",
			cmd);

		TCLAP::ValueArg<std::string> genomesFileName("",
			"gfile",
			"FASTA file with genomes",
			true,
			"",
			"file name",
			cmd);

		TCLAP::ValueArg<std::string> outDirName("o",
			"outdir",
			"Output dir name prefix",
			false,
			"out",
			"file name",
			cmd);

		TCLAP::ValueArg<std::string> dumpFileName("d",
			"dumpfile",
			"Dump file name",
			false,
			"dump.dot",
			"file name",
			cmd);

		cmd.parse(argc, argv);

		std::vector<std::vector<Sibelia::Edge> > lightSyntenyPath;
		Sibelia::EdgeStorage storage(inFileName.getValue(), genomesFileName.getValue(), kvalue.getValue());
		Sibelia::FindLightPaths(storage, 
			minBlockSize.getValue(),
			maxBranchSize.getValue(),
			maxFlankingSize.getValue(),
			lookingDepth.getValue(),
			sampleSize.getValue(),
			threads.getValue(),
			lightSyntenyPath);
//		Sibelia::BlocksFinder finder(storage, kvalue.getValue());		
/*		finder.FindBlocks(minBlockSize.getValue(),
			maxBranchSize.getValue(),
			maxFlankingSize.getValue(),
			lookingDepth.getValue(),
			sampleSize.getValue(),
			threads.getValue(),
			outDirName.getValue() + "/paths.txt");*/
//		finder.GenerateLegacyOutput(outDirName.getValue());
		std::ofstream lightDumpStream(outDirName.getValue() + "/light_graph.dot");
		storage.Dump(lightDumpStream, lightSyntenyPath);
	}
	catch (TCLAP::ArgException & e)
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}
	catch (std::runtime_error & e)
	{
		std::cerr << "error: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}

