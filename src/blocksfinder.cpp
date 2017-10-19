#include "blocksfinder.h"


namespace Sibelia
{ 
	#include <errno.h>
	#include <sys/types.h>
	#include <sys/stat.h>

	#ifdef _WIN32
		#include <direct.h>
	#endif

	void CreateOutDirectory(const std::string & path)
	{
		int result = 0;
#ifdef _WIN32
		result = _mkdir(path.c_str());
#else
		result = mkdir(path.c_str(), 0755);
#endif
		if (result != 0 && errno != EEXIST)
		{
			throw std::runtime_error(("Cannot create dir " + path).c_str());
		}
	}

	const int32_t BlocksFinder::Assignment::UNKNOWN_BLOCK = INT32_MAX;

	bool compareById(const BlockInstance & a, const BlockInstance & b)
	{
		return CompareBlocks(a, b, &BlockInstance::GetBlockId);
	}

	bool compareByChrId(const BlockInstance & a, const BlockInstance & b)
	{
		return CompareBlocks(a, b, &BlockInstance::GetChrId);
	}

	bool compareByStart(const BlockInstance & a, const BlockInstance & b)
	{
		return CompareBlocks(a, b, &BlockInstance::GetChrId);
	}
	
	const std::string DELIMITER(80, '-');

	int BlockInstance::GetSignedBlockId() const
	{
		return id_;
	}

	bool BlockInstance::GetDirection() const
	{
		return id_ > 0;
	}

	int BlockInstance::GetSign() const
	{
		return GetSignedBlockId() > 0 ? +1 : -1;
	}

	int BlockInstance::GetBlockId() const
	{
		return abs(id_);
	}

	size_t BlockInstance::GetChrId() const
	{
		return chr_;
	}

	size_t BlockInstance::GetStart() const
	{
		return start_;
	}

	size_t BlockInstance::GetEnd() const
	{
		return end_;
	}

	size_t BlockInstance::GetConventionalStart() const
	{
		if (GetDirection())
		{
			return start_ + 1;
		}

		return end_;
	}

	size_t BlockInstance::GetConventionalEnd() const
	{
		if (GetDirection())
		{
			return end_;
		}

		return start_ + 1;
	}

	std::pair<size_t, size_t> BlockInstance::CalculateOverlap(const BlockInstance & instance) const
	{
		if (GetChrId() == instance.GetChrId())
		{
			size_t overlap = 0;
			if (GetStart() >= instance.GetStart() && GetStart() <= instance.GetEnd())
			{
				return std::pair<size_t, size_t>(GetStart(), std::min(GetEnd(), instance.GetEnd()));
			}

			if (instance.GetStart() >= GetStart() && instance.GetStart() <= GetEnd())
			{
				return std::pair<size_t, size_t>(instance.GetStart(), std::min(GetEnd(), instance.GetEnd()));
			}
		}

		return std::pair<size_t, size_t>(0, 0);
	}

	bool BlockInstance::operator == (const BlockInstance & toCompare) const
	{
		return start_ == toCompare.start_ && end_ == toCompare.end_ && GetChrId() == toCompare.GetChrId() && id_ == toCompare.id_;
	}

	bool BlockInstance::operator != (const BlockInstance & toCompare) const
	{
		return !(*this == toCompare);
	}

	void BlockInstance::Reverse()
	{
		id_ = -id_;
	}

	size_t BlockInstance::GetLength() const
	{
		return end_ - start_;
	}

	bool BlockInstance::operator < (const BlockInstance & toCompare) const
	{
		return std::make_pair(GetBlockId(), std::make_pair(GetChrId(), GetStart())) < std::make_pair(toCompare.GetBlockId(), std::make_pair(toCompare.GetChrId(), toCompare.GetStart()));
	}

	void BlocksFinder::GenerateReport(const BlockList & block, const std::string & fileName) const
	{
		std::ofstream out;
		TryOpenFile(fileName, out);
		GroupedBlockList sepBlock;
		std::vector<IndexPair> group;
		BlockList blockList = block;
		GroupBy(blockList, compareById, std::back_inserter(group));
		for (std::vector<IndexPair>::iterator it = group.begin(); it != group.end(); ++it)
		{
			sepBlock.push_back(std::make_pair(it->second - it->first, std::vector<BlockInstance>(blockList.begin() + it->first, blockList.begin() + it->second)));
		}

		ListChrs(out);
		out << "Degree\tCount\tTotal";
		for (size_t i = 0; i < storage_.GetChrNumber(); i++)
		{
			out << "\tSeq " << i + 1;
		}

		out << std::endl;
		group.clear();
		GroupBy(sepBlock, ByFirstElement, std::back_inserter(group));
		group.push_back(IndexPair(0, sepBlock.size()));
		for (std::vector<IndexPair>::iterator it = group.begin(); it != group.end(); ++it)
		{
			if (it != group.end() - 1)
			{
				out << sepBlock[it->first].first << '\t' << it->second - it->first << '\t';
			}
			else
			{
				out << "All\t" << it->second - it->first << "\t";
			}

			out.precision(2);
			out.setf(std::ostream::fixed);
			std::vector<double> coverage = CalculateCoverage(sepBlock.begin() + it->first, sepBlock.begin() + it->second);
			std::copy(coverage.begin(), coverage.end(), std::ostream_iterator<double>(out, "%\t"));
			out << std::endl;
		}

		out << DELIMITER << std::endl;
	}

	std::vector<double> BlocksFinder::CalculateCoverage(GroupedBlockList::const_iterator start, GroupedBlockList::const_iterator end) const
	{
		std::vector<double> ret;
		std::vector<bool> cover;
		double totalBp = 0;
		double totalCoveredBp = 0;
		for (size_t chr = 0; chr < storage_.GetChrNumber(); chr++)
		{
			totalBp += storage_.GetChrSequence(chr).size();
			cover.assign(storage_.GetChrSequence(chr).size(), 0);
			for (GroupedBlockList::const_iterator it = start; it != end; ++it)
			{
				for (size_t i = 0; i < it->second.size(); i++)
				{
					if (it->second[i].GetChrId() == chr)
					{
						std::fill(cover.begin() + it->second[i].GetStart(), cover.begin() + it->second[i].GetEnd(), COVERED);
					}
				}
			}

			double nowCoveredBp = static_cast<double>(std::count(cover.begin(), cover.end(), COVERED));
			ret.push_back(nowCoveredBp / cover.size() * 100);
			totalCoveredBp += nowCoveredBp;
		}

		ret.insert(ret.begin(), totalCoveredBp / totalBp * 100);
		return ret;
	}


	std::string BlocksFinder::OutputIndex(const BlockInstance & block) const
	{
		std::stringstream out;
		out << block.GetChrId() + 1 << '\t' << (block.GetSignedBlockId() < 0 ? '-' : '+') << '\t';
		out << block.GetConventionalStart() << '\t' << block.GetConventionalEnd() << '\t' << block.GetEnd() - block.GetStart();
		return out.str();
	}


	void BlocksFinder::OutputBlocks(const std::vector<BlockInstance>& block, std::ofstream& out) const
	{
		std::vector<IndexPair> group;
		std::vector<BlockInstance> blockList = block;
		GroupBy(blockList, compareById, std::back_inserter(group));
		for (std::vector<IndexPair>::iterator it = group.begin(); it != group.end(); ++it)
		{
			size_t length = it->second - it->first;
			std::sort(blockList.begin() + it->first, blockList.begin() + it->second, compareByChrId);
			out << "Block #" << blockList[it->first].GetBlockId() << std::endl;
			out << "Seq_id\tStrand\tStart\tEnd\tLength" << std::endl;
			for (auto jt = blockList.begin() + it->first; jt < blockList.begin() + it->first + length; ++jt)
			{
				out << OutputIndex(*jt) << std::endl;
			}

			out << DELIMITER << std::endl;
		}
	}


	void BlocksFinder::ListBlocksIndices(const BlockList & block, const std::string & fileName) const
	{
		std::ofstream out;
		TryOpenFile(fileName, out);
		ListChrs(out);
		OutputBlocks(block, out);
	}

	void BlocksFinder::ListChromosomesAsPermutations(const BlockList & block, const std::string & fileName) const
	{
		std::ofstream out;
		TryOpenFile(fileName, out);
		std::vector<IndexPair> group;
		BlockList blockList = block;
		GroupBy(blockList, compareByChrId, std::back_inserter(group));
		for (std::vector<IndexPair>::iterator it = group.begin(); it != group.end(); ++it)
		{
			out.setf(std::ios_base::showpos);
			size_t length = it->second - it->first;
			size_t chr = blockList[it->first].GetChrId();
			out << '>' << storage_.GetChrDescription(chr) << std::endl;
			std::sort(blockList.begin() + it->first, blockList.begin() + it->second);
			for (auto jt = blockList.begin() + it->first; jt < blockList.begin() + it->first + length; ++jt)
			{
				out << jt->GetSignedBlockId() << " ";
			}

			out << "$" << std::endl;
		}
	}


	void BlocksFinder::TryOpenFile(const std::string & fileName, std::ofstream & stream) const
	{
		stream.open(fileName.c_str());
		if (!stream)
		{
			throw std::runtime_error(("Cannot open file " + fileName).c_str());
		}
	}

	void BlocksFinder::ListChrs(std::ostream & out) const
	{
		out << "Seq_id\tSize\tDescription" << std::endl;
		for (size_t i = 0; i < storage_.GetChrNumber(); i++)
		{
			out << i + 1 << '\t' << storage_.GetChrSequence(i).size() << '\t' << storage_.GetChrDescription(i) << std::endl;
		}

		out << DELIMITER << std::endl;
	}

	void BlocksFinder::Dump(std::ostream & out) const
	{
		out << "digraph G\n{\nrankdir = LR" << std::endl;
		for (size_t i = 0; i < storage_.GetChrNumber(); i++)
		{
			for (auto it = storage_.Begin(i); it != storage_.End(i) - 1; ++it)
			{
				auto jt = it + 1;
				out << it.GetVertexId(&storage_) << " -> " << jt.GetVertexId(&storage_)
					<< "[label=\"" << it.GetChar(&storage_) << ", " << it.GetChrId() << ", " << it.GetPosition(&storage_) << "\" color=blue]\n";
				out << jt.Reverse().GetVertexId(&storage_) << " -> " << it.Reverse().GetVertexId(&storage_)
					<< "[label=\"" << it.GetChar(&storage_) << ", " << it.GetChrId() << ", " << it.GetPosition(&storage_) << "\" color=red]\n";
			}
		}

		for (size_t i = 0; i < syntenyPath_.size(); i++)
		{
			for (size_t j = 0; j < syntenyPath_[i].size(); j++)
			{
				Edge e = syntenyPath_[i][j];
				out << e.GetStartVertex() << " -> " << e.GetEndVertex() <<
					"[label=\"" << e.GetChar() << ", " << i + 1 << "\" color=green]\n";
				e = e.Reverse();
				out << e.GetStartVertex() << " -> " << e.GetEndVertex() <<
					"[label=\"" << e.GetChar() << ", " << -(int64_t(i + 1)) << "\" color=green]\n";
			}
		}

		out << "}" << std::endl;
	}

	void BlocksFinder::ListBlocksSequences(const BlockList & block, const std::string & fileName) const
	{
		std::ofstream out;
		TryOpenFile(fileName, out);
		std::vector<IndexPair> group;
		BlockList blockList = block;
		GroupBy(blockList, compareById, std::back_inserter(group));
		for (std::vector<IndexPair>::iterator it = group.begin(); it != group.end(); ++it)
		{
			for (size_t block = it->first; block < it->second; block++)
			{
				size_t length = blockList[block].GetLength();
				char strand = blockList[block].GetSignedBlockId() > 0 ? '+' : '-';
				size_t chr = blockList[block].GetChrId();
				out << ">Seq=\"" << storage_.GetChrDescription(chr) << "\",Strand='" << strand << "',";
				out << "Block_id=" << blockList[block].GetBlockId() << ",Start=";
				out << blockList[block].GetConventionalStart() << ",End=" << blockList[block].GetConventionalEnd() << std::endl;

				if (blockList[block].GetSignedBlockId() > 0)
				{
					OutputLines(storage_.GetChrSequence(chr).begin() + blockList[block].GetStart(), length, out);
				}
				else
				{
					std::string::const_reverse_iterator it(storage_.GetChrSequence(chr).begin() + blockList[block].GetEnd());
					OutputLines(CFancyIterator(it, TwoPaCo::DnaChar::ReverseChar, ' '), length, out);
				}

				out << std::endl;
			}
		}
	}

	void BlocksFinder::GenerateLegacyOutput(const std::string & outDir, const std::string & oldCoords) const
	{
		std::vector<std::vector<bool> > covered(storage_.GetChrNumber());
		for (size_t i = 0; i < covered.size(); i++)
		{
			covered[i].assign(storage_.GetChrSequence(i).size(), false);
		}

		BlockList instance;
		for (size_t chr = 0; chr < blockId_.size(); chr++)
		{
			for (size_t i = 0; i < blockId_[chr].size();)
			{
				if (blockId_[chr][i].block != Assignment::UNKNOWN_BLOCK)
				{
					int64_t bid = blockId_[chr][i].block;
					size_t j = i;
					for (; j < blockId_[chr].size() && blockId_[chr][i] == blockId_[chr][j]; j++);
					j--;
					int64_t cstart = storage_.GetIterator(chr, i, bid > 0).GetPosition(&storage_);
					int64_t cend = storage_.GetIterator(chr, j, bid > 0).GetPosition(&storage_) + (bid > 0 ? k_ : -k_);
					int64_t start = std::min(cstart, cend);
					int64_t end = std::max(cstart, cend);
					instance.push_back(BlockInstance(bid, chr, start, end));
					std::fill(covered[chr].begin() + start, covered[chr].begin() + end, true);
					i = j + 1;
				}
				else
				{
					++i;
				}
			}
		}

		CreateOutDirectory(outDir);
		GenerateReport(instance, outDir + "/" + "coverage_report.txt");
		ListBlocksIndices(instance, outDir + "/" + "blocks_coords.txt");
		ListBlocksSequences(instance, outDir + "/" + "blocks_sequences.fasta");
	}
}