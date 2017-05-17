#ifndef _TRASERVAL_H_
#define _TRAVERSAL_H_

//#define _DEBUG_OUT

#include "junctionstorage.h"

#include <set>
#include <map>
#include <list>
#include <deque>
#include <cassert>
#include <numeric>
#include <sstream>
#include <iostream>

namespace Sibelia
{
	
	extern const std::string DELIMITER;

	class BlockInstance
	{
	public:
		BlockInstance() {}
		BlockInstance(int id, const size_t chr, size_t start, size_t end) : id_(id), chr_(chr), start_(start), end_(end) {}
		void Reverse();
		int GetSignedBlockId() const;
		bool GetDirection() const;
		int GetBlockId() const;		
		int GetSign() const;
		size_t GetChrId() const;
		size_t GetStart() const;
		size_t GetEnd() const;
		size_t GetLength() const;
		size_t GetConventionalStart() const;
		size_t GetConventionalEnd() const;
		std::pair<size_t, size_t> CalculateOverlap(const BlockInstance & instance) const;
		bool operator < (const BlockInstance & toCompare) const;
		bool operator == (const BlockInstance & toCompare) const;
		bool operator != (const BlockInstance & toCompare) const;
	private:
		int id_;
		size_t start_;
		size_t end_;
		size_t chr_;
	};

	namespace
	{
		const bool COVERED = true;
		typedef std::vector<BlockInstance> BlockList;
		typedef std::pair<size_t, std::vector<BlockInstance> > GroupedBlock;
		typedef std::vector<GroupedBlock> GroupedBlockList;
		bool ByFirstElement(const GroupedBlock & a, const GroupedBlock & b)
		{
			return a.first < b.first;
		}

		std::string IntToStr(size_t x)
		{
			std::stringstream ss;
			ss << x;
			return ss.str();
		}

		template<class Iterator1, class Iterator2>
		void CopyN(Iterator1 it, size_t count, Iterator2 out)
		{
			for (size_t i = 0; i < count; i++)
			{
				*out++ = *it++;
			}
		}

		template<class Iterator>
		Iterator AdvanceForward(Iterator it, size_t step)
		{
			std::advance(it, step);
			return it;
		}

		template<class Iterator>
		Iterator AdvanceBackward(Iterator it, size_t step)
		{
			for (size_t i = 0; i < step; i++)
			{
				--it;
			}

			return it;
		}


		typedef std::pair<size_t, size_t> IndexPair;
		template<class T, class F, class It>
		void GroupBy(std::vector<T> & store, F pred, It out)
		{
			sort(store.begin(), store.end(), pred);
			for (size_t now = 0; now < store.size();)
			{
				size_t prev = now;
				for (; now < store.size() && !pred(store[prev], store[now]); now++);
				*out++ = std::make_pair(prev, now);
			}
		}

		template<class F>
		bool CompareBlocks(const BlockInstance & a, const BlockInstance & b, F f)
		{
			return (a.*f)() < (b.*f)();
		}

		template<class F>
		bool EqualBlocks(const BlockInstance & a, const BlockInstance & b, F f)
		{
			return f(a) == f(b);
		}

		template<class Iterator, class F, class ReturnType>
		struct FancyIterator : public std::iterator<std::forward_iterator_tag, ReturnType>
		{
		public:
			FancyIterator& operator++()
			{
				++it;
				return *this;
			}

			FancyIterator operator++(int)
			{
				FancyIterator ret(*this);
				++(*this);
				return ret;
			}

			bool operator == (FancyIterator toCompare) const
			{
				return it == toCompare.it;
			}

			bool operator != (FancyIterator toCompare) const
			{
				return !(*this == toCompare);
			}

			ReturnType operator * ()
			{
				return f(*it);
			}

			FancyIterator() {}
			FancyIterator(Iterator it, F f) : it(it), f(f) {}

		private:
			F f;
			Iterator it;
		};

		template<class Iterator, class F, class ReturnType>
		FancyIterator<Iterator, F, ReturnType> CFancyIterator(Iterator it, F f, ReturnType)
		{
			return FancyIterator<Iterator, F, ReturnType>(it, f);
		}

	}

	bool compareById(const BlockInstance & a, const BlockInstance & b);
	bool compareByChrId(const BlockInstance & a, const BlockInstance & b);
	bool compareByStart(const BlockInstance & a, const BlockInstance & b);
	
	void CreateOutDirectory(const std::string & path);

	class BlocksFinder
	{
	public:

		BlocksFinder(const JunctionStorage & storage, size_t k) : storage_(storage), k_(k)
		{
			scoreFullChains_ = true;
		}

		void FindBlocks(int64_t minBlockSize, int64_t maxBranchSize, int64_t flankingThreshold, int64_t lookingDepth, const std::string & debugOut)
		{
			blocksFound_ = 0;
			lookingDepth_ = lookingDepth;
			minBlockSize_ = minBlockSize;
			maxBranchSize_ = maxBranchSize;			
			flankingThreshold_ = flankingThreshold;
			std::vector<std::pair<int64_t, int64_t> > bubbleCountVector;

			blockId_.resize(storage_.GetChrNumber());
			for (size_t i = 0; i < storage_.GetChrNumber(); i++)
			{
				blockId_[i].resize(storage_.GetChrVerticesCount(i));
			}
			
			std::map<int64_t, int64_t> bubbleCount;
			for (int64_t vid = -storage_.GetVerticesNumber() + 1; vid < storage_.GetVerticesNumber(); vid++)
			{
				CountBubbles(vid, bubbleCount);
			}

			for (auto it = bubbleCount.rbegin(); it != bubbleCount.rend(); ++it)
			{
				bubbleCountVector.push_back(std::make_pair(it->second, it->first));
			}

			std::sort(bubbleCountVector.begin(), bubbleCountVector.end());
			int count = 0;
			std::ofstream debugStream(debugOut.c_str());
			for (auto it = bubbleCountVector.rbegin(); it != bubbleCountVector.rend(); ++it)
			{
				if (count++ % 1000 == 0)
				{
					std::cerr << count << '\t' << bubbleCountVector.size() << std::endl;
				}

				ExtendSeed(it->second, bubbleCount, debugStream);
			}
		}

		void Dump(std::ostream & out) const
		{
			out << "digraph G\n{\nrankdir = LR" << std::endl;
			for (size_t i = 0; i < storage_.GetChrNumber(); i++)
			{
				for (auto it = storage_.Begin(i); it != storage_.End(i) - 1; ++it)
				{
					auto jt = it + 1;
					out << it.GetVertexId() << " -> " << jt.GetVertexId() 
						<< "[label=\"" << it.GetChar() << ", " << it.GetChrId() << ", " << it.GetPosition() << "\" color=blue]" << std::endl;
					out << jt.Reverse().GetVertexId() << " -> " << it.Reverse().GetVertexId() 
						<< "[label=\"" << it.GetChar() << ", " << it.GetChrId() << ", " << it.GetPosition() << "\" color=red]" << std::endl;
				}
			}

			for (size_t i = 0; i < syntenyPath_.size(); i++)
			{
				for (size_t j = 0; j < syntenyPath_[i].size() - 1; j++)
				{
					out << syntenyPath_[i][j] << " -> " << syntenyPath_[i][j + 1] << "[label=\"" << i + 1 << "\" color=green]" << std::endl;
					out << -syntenyPath_[i][j + 1] << " -> " << -syntenyPath_[i][j] << "[label=\"" << -int64_t(i + 1) << "\" color=green]" << std::endl;
				}
			}

			out << "}" << std::endl;
		}

		void ListBlocksSequences(const BlockList & block, const std::string & fileName) const
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


		void GenerateLegacyOutput(const std::string & outDir) const
		{
			BlockList instance;
			std::vector<std::vector<bool> > covered(storage_.GetChrNumber());
			for (size_t i = 0; i < covered.size(); i++)
			{
				covered[i].assign(storage_.GetChrSequence(i).size(), false);
			}

			for (size_t chr = 0; chr < blockId_.size(); chr++)
			{
				for (size_t i = 0; i < blockId_[chr].size();)
				{
					if (blockId_[chr][i].block != UNKNOWN_BLOCK)
					{
						int64_t bid = blockId_[chr][i].block;
						size_t j = i;
						for (; j < blockId_[chr].size() && blockId_[chr][i] == blockId_[chr][j]; j++);
						j--;
						int64_t cstart = storage_.GetIterator(chr, i, bid > 0).GetPosition();
						int64_t cend = storage_.GetIterator(chr, j, bid > 0).GetPosition() + (bid > 0 ? k_ : -k_);
						int64_t start = std::min(cstart, cend);
						int64_t end = std::max(cstart, cend);						
						instance.push_back(BlockInstance(bid, chr, start, end));
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


	private:

		static const int64_t UNKNOWN_BLOCK;

		struct Assignment
		{
			int64_t block;
			int64_t instance;
			Assignment() : block(UNKNOWN_BLOCK), instance(UNKNOWN_BLOCK)
			{

			}

			bool operator == (const Assignment & assignment) const
			{
				return block == assignment.block && instance == assignment.instance;
			}
		};

		template<class Iterator>
		void OutputLines(Iterator start, size_t length, std::ostream & out) const
		{
			for (size_t i = 1; i <= length; i++, ++start)
			{
				out << *start;
				if (i % 80 == 0 && i != length)
				{
					out << std::endl;
				}
			}
		}

		void GenerateReport(const BlockList & block, const std::string & fileName) const
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

		std::vector<double> CalculateCoverage(GroupedBlockList::const_iterator start, GroupedBlockList::const_iterator end) const
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


		std::string OutputIndex(const BlockInstance & block) const
		{
			std::stringstream out;
			out << block.GetChrId() + 1 << '\t' << (block.GetSignedBlockId() < 0 ? '-' : '+') << '\t';
			out << block.GetConventionalStart() << '\t' << block.GetConventionalEnd() << '\t' << block.GetEnd() - block.GetStart();
			return out.str();
		}


		void OutputBlocks(const std::vector<BlockInstance>& block, std::ofstream& out) const
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


		void ListBlocksIndices(const BlockList & block, const std::string & fileName) const
		{
			std::ofstream out;
			TryOpenFile(fileName, out);
			ListChrs(out);
			OutputBlocks(block, out);
		}

		void ListChromosomesAsPermutations(const BlockList & block, const std::string & fileName) const
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


		void TryOpenFile(const std::string & fileName, std::ofstream & stream) const
		{
			stream.open(fileName.c_str());
			if (!stream)
			{
				throw std::runtime_error(("Cannot open file " + fileName).c_str());
			}
		}

		void ListChrs(std::ostream & out) const
		{
			out << "Seq_id\tSize\tDescription" << std::endl;
			for (size_t i = 0; i < storage_.GetChrNumber(); i++)
			{
				out << i + 1 << '\t' << storage_.GetChrSequence(i).size() << '\t' << storage_.GetChrDescription(i) << std::endl;
			}

			out << DELIMITER << std::endl;
		}

		struct BranchData
		{
			BranchData() {}
			std::vector<size_t> branchId;
		};

		typedef std::vector< std::vector<size_t> > BubbledBranches;

		struct Path
		{
		public:
			Path() {}
			Path(int64_t start,
				const JunctionStorage & storage,
				int64_t maxBranchSize,
				int64_t minBlockSize,
				int64_t maxFlankingSize,
				const std::vector<std::vector<Assignment> > & blockId) :
				maxBranchSize_(maxBranchSize), minBlockSize_(minBlockSize), maxFlankingSize_(maxFlankingSize), storage_(&storage),
				minChainSize_(minBlockSize - 2 * maxFlankingSize), blockId_(&blockId)
			{
				PointPushBack(Edge(0, start, 0, 0));
			}

			struct Instance
			{
				int64_t leftFlankDistance;
				int64_t rightFlankDistance;
				std::deque<JunctionStorage::JunctionIterator> seq;
			};

			struct Point
			{
				int64_t vertex;
				int64_t distance;
				Point() {}
				Point(int64_t vertex, int64_t distance) : vertex(vertex), distance(distance) {}
			};

			void PrintInstance(const Instance & inst, std::ostream & out) const
			{
				for (auto & it : inst.seq)
				{
					out << "{vid:" << it.GetVertexId()
						<< " str:" << inst.seq.front().IsPositiveStrand()
						<< " chr:" << inst.seq.front().GetChrId()
						<< " pos:" << it.GetPosition() << "} ";
				}

				out << std::endl;
			}

			void DebugOut(std::ostream & out, bool all = true) const
			{
				out << "Path: ";
				for (auto & pt : body_)
				{
					out << pt.vertex << ' ';
				}

				out << std::endl << "Instances: " << std::endl;
				for (auto & inst : instance_)
				{	
					if (all || IsGoodInstance(inst))
					{
						PrintInstance(inst, out);
					}
				}

				out << std::endl;
			}			

			bool PointPushBack(const Edge & e)
			{
				int64_t vertex = e.GetEndVertex();
				if (FindVertexInPath(vertex) != body_.end())
				{
					return false;
				}

				size_t j = 0;
				std::vector<int64_t> nextRightFlank(instance_.size());
				int64_t vertexDistance = body_.empty() ? 0 : body_.back().distance + e.GetLength();
				for (auto & inst : instance_)
				{
					nextRightFlank[j++] = inst.rightFlankDistance;
				}

				for (size_t i = 0; i < storage_->GetInstancesCount(vertex); i++)
				{
					j = 0;
					JunctionStorage::JunctionIterator it = storage_->GetJunctionInstance(vertex, i);
					if ((*blockId_)[it.GetChrId()][it.GetIndex()].block != UNKNOWN_BLOCK || 
						inside_.count(std::make_pair(it.GetChrId(), it.GetIndex())))
					{
						continue;
					}

					for (auto & inst : instance_)
					{
						if (Compatible(inst.seq.back(), it, e))
						{
							int64_t leftFlank = abs(inst.leftFlankDistance - body_.front().distance);
							if (abs(it.GetPosition() - inst.seq.front().GetPosition()) >= minChainSize_ && leftFlank > maxFlankingSize_)
							{
								return false;
							}

							nextRightFlank[j] = vertexDistance;
							break;
						}

						j++;
					}
				}

				auto it = instance_.begin();
				for (int64_t & rightFlank : nextRightFlank)
				{
					if (abs(it->seq.front().GetPosition() - it->seq.back().GetPosition()) >= minChainSize_ && abs(vertexDistance - rightFlank) > maxFlankingSize_)
					{
						return false;
					}

					++it;
				}

				for (size_t i = 0; i < storage_->GetInstancesCount(vertex); i++)
				{
					bool newInstance = true;
					JunctionStorage::JunctionIterator it = storage_->GetJunctionInstance(vertex, i);
					if ((*blockId_)[it.GetChrId()][it.GetIndex()].block != UNKNOWN_BLOCK || 
						inside_.count(std::make_pair(it.GetChrId(), it.GetIndex())))
					{
						continue;
					}

					for (auto & inst : instance_)
					{
						if (Compatible(inst.seq.back(), it, e))
						{
							newInstance = false;
							inst.seq.push_back(it);
							inst.rightFlankDistance = vertexDistance;
							inside_.insert(std::make_pair(it.GetChrId(), it.GetIndex()));
							break;
						}
					}

					if (newInstance)
					{
						instance_.push_back(Instance());
						instance_.back().seq.push_back(it);
						instance_.back().leftFlankDistance = instance_.back().rightFlankDistance = vertexDistance;
					}
				}

				body_.push_back(Point(vertex, vertexDistance));
				return true;
			}

			bool PointPushFront(const Edge & e)
			{
				int64_t vertex = e.GetStartVertex();
				if (FindVertexInPath(vertex) != body_.end())
				{
					return false;
				}

				size_t j = 0;
				std::vector<int64_t> nextLeftFlank(instance_.size());
				int64_t vertexDistance = body_.empty() ? 0 : body_.front().distance - e.GetLength();
				for (auto & inst : instance_)
				{
					nextLeftFlank[j++] = inst.leftFlankDistance;
				}

				for (size_t i = 0; i < storage_->GetInstancesCount(vertex); i++)
				{
					j = 0;
					JunctionStorage::JunctionIterator it = storage_->GetJunctionInstance(vertex, i);
					if ((*blockId_)[it.GetChrId()][it.GetIndex()].block != UNKNOWN_BLOCK ||
						inside_.count(std::make_pair(it.GetChrId(), it.GetIndex())))
					{
						continue;
					}

					for (auto & inst : instance_)
					{
						if (Compatible(it, inst.seq.front(), e))
						{
							int64_t rightFlank = abs(inst.rightFlankDistance - body_.back().distance);
							if (abs(it.GetPosition() - inst.seq.back().GetPosition()) >= minChainSize_ && rightFlank > maxFlankingSize_)
							{
								return false;
							}

							nextLeftFlank[j] = vertexDistance;
							break;
						}

						j++;
					}
				}

				auto it = instance_.begin();
				for (int64_t leftFlank : nextLeftFlank)
				{
					if (abs(it->seq.front().GetPosition() - it->seq.back().GetPosition()) >= minChainSize_ && abs(vertexDistance - leftFlank) > maxFlankingSize_)
					{
						return false;
					}

					++it;
				}				

				for (size_t i = 0; i < storage_->GetInstancesCount(vertex); i++)
				{
					bool newInstance = true;
					JunctionStorage::JunctionIterator it = storage_->GetJunctionInstance(vertex, i);
					if ((*blockId_)[it.GetChrId()][it.GetIndex()].block != UNKNOWN_BLOCK || 
						inside_.count(std::make_pair(it.GetChrId(), it.GetIndex())))
					{
						continue;
					}

					for (auto & inst : instance_)
					{
						if (Compatible(it, inst.seq.front(), e))
						{
							newInstance = false;
							inst.seq.push_front(it);
							inst.leftFlankDistance = vertexDistance;
							inside_.insert(std::make_pair(it.GetChrId(), it.GetIndex()));
							break;
						}
					}

					if (newInstance)
					{
						instance_.push_back(Instance());
						instance_.back().seq.push_back(it);
						instance_.back().leftFlankDistance = instance_.back().rightFlankDistance = vertexDistance;
					}
				}

				body_.push_front(Point(vertex, vertexDistance));
				return true;
			}

			size_t PathLength() const
			{
				return body_.size();
			}

			const std::list<Instance> & Instances() const
			{
				return instance_;
			}

			const std::deque<Point> & PathBody() const
			{
				return body_;
			}

			int64_t MiddlePathLength() const
			{
				return body_.back().distance - body_.front().distance;
			}

			int64_t GetVertex(size_t index) const
			{
				return body_[index].vertex;
			}

			void PointPopFront()
			{
				int64_t newLeftFlankDistance = (++body_.begin())->distance;
				for (auto it = instance_.begin(); it != instance_.end();)
				{
					if (it->seq.front().GetVertexId() == body_.front().vertex)
					{
						JunctionStorage::JunctionIterator & j = it->seq.front();
						inside_.erase(std::make_pair(j.GetChrId(), j.GetIndex()));
						it->seq.pop_front();
						if (it->seq.empty())
						{
							it = instance_.erase(it);
						}
						else
						{
							++it->leftFlankDistance = newLeftFlankDistance;
						}
					}
					else
					{
						++it;
					}
				}

				body_.pop_front();
			}

			void PointPopBack()
			{
				int64_t newRightFlankDistance = (--(--body_.end()))->distance;
				for (auto it = instance_.begin(); it != instance_.end();)
				{
					if (it->seq.back().GetVertexId() == body_.back().vertex)
					{
						JunctionStorage::JunctionIterator & j = it->seq.back();
						inside_.erase(std::make_pair(j.GetChrId(), j.GetIndex()));
						it->seq.pop_back();
						if (it->seq.empty())
						{
							it = instance_.erase(it);
						}
						else
						{
							it++->rightFlankDistance = newRightFlankDistance;
						}
					}
					else
					{
						++it;
					}
				}

				body_.pop_back();
			}

			int64_t Score(bool final = false) const
			{
				int64_t score;
				int64_t length;
				int64_t ret = 0;
				for (auto & inst : instance_)
				{
					InstanceScore(inst, length, score);
					if (!final || length >= minChainSize_)
					{
						ret += score;
					}
				}

				return ret;
			}

			int64_t GoodInstances() const
			{
				int64_t ret = 0;
				for (auto & it : instance_)
				{
					if (IsGoodInstance(it))
					{
						ret++;
					}
				}

				return ret;
			}

			bool IsGoodInstance(const Instance & it) const
			{
				int64_t score;
				int64_t length;
				InstanceScore(it, length, score);
				return length >= minChainSize_;
			}

			void InstanceScore(const Instance & inst, int64_t & length, int64_t & score) const
			{
				int64_t leftFlank = abs(inst.leftFlankDistance - body_.front().distance);
				int64_t rightFlank = abs(inst.rightFlankDistance - body_.back().distance);
				length = abs(inst.seq.front().GetPosition() - inst.seq.back().GetPosition());
				score = length - leftFlank - rightFlank;
			}

		private:

			bool Compatible(JunctionStorage::JunctionIterator start, JunctionStorage::JunctionIterator end, Edge e) const
			{			
				if (start.GetChrId() != end.GetChrId() || start.IsPositiveStrand() != end.IsPositiveStrand())
				{
					return false;
				}				

				int64_t diff = end.GetPosition() - start.GetPosition();
				if (start.IsPositiveStrand())
				{
					if (diff < 0)
					{
						return false;
					}

					auto start1 = start + 1;
					if (diff > maxBranchSize_ && (start.GetChar() != e.GetChar() || end != start1 || start1.GetVertexId() != e.GetEndVertex()))
					{
						return false;
					}
				}
				else
				{
					if (-diff < 0)
					{
						return false;
					}
					
					auto start1 = start + 1;
					if (-diff > maxBranchSize_ && (start.GetChar() != e.GetChar() || end != start1 || start1.GetVertexId() != e.GetEndVertex()))
					{
						return false;
					}
				}				
				
				return true;
			}						

			std::set<std::pair<int64_t, int64_t> > inside_;
			std::deque<Point> body_;
			std::list<Instance> instance_;
			int64_t minChainSize_;
			int64_t minBlockSize_;
			int64_t maxBranchSize_;
			int64_t maxFlankingSize_;			
			const JunctionStorage * storage_;
			const std::vector<std::vector<Assignment> > * blockId_;

			std::deque<Point>::const_iterator FindVertexInPath(int64_t vertex) const
			{
				for (auto it = body_.begin(); it != body_.end(); ++it)
				{
					if (it->vertex == vertex)
					{
						return it;
					}
				}
				
				return body_.end();
			}			
		};

		void ExtendSeed(int64_t vertex, const std::map<int64_t, int64_t> & bubbleCount, std::ostream & debugOut)
		{
			Path bestPath(vertex, storage_, maxBranchSize_, minBlockSize_, flankingThreshold_, blockId_);
			while (true)
			{
				Path currentPath = bestPath;
				int64_t prevBestScore = bestPath.Score(scoreFullChains_);
				ExtendPathBackward(currentPath, bestPath, lookingDepth_);	
				currentPath = bestPath;				
				ExtendPathForward(currentPath, bestPath, lookingDepth_);
				if (bestPath.Score(scoreFullChains_) <= prevBestScore)
				{
					break;
				}
			}

			if (bestPath.Score(true) > 0 && bestPath.MiddlePathLength() >= minBlockSize_ && bestPath.GoodInstances() > 1)
			{					
				debugOut << "Block No." << ++blocksFound_ << ":"  << std::endl;
				bestPath.DebugOut(debugOut, false);
				syntenyPath_.push_back(std::vector<int64_t>());
				for (auto pt : bestPath.PathBody())
				{					
					syntenyPath_.back().push_back(pt.vertex);
				}

				for (size_t i = 0; i < syntenyPath_.back().size() - 1; i++)
				{
					forbidden_.insert(LightEdge(syntenyPath_.back()[i], syntenyPath_.back()[i + 1]));
					forbidden_.insert(LightEdge(-syntenyPath_.back()[i + 1], -syntenyPath_.back()[i]));
				}

				int64_t instanceCount = 0;
				for (auto & instance : bestPath.Instances())
				{
					if (bestPath.IsGoodInstance(instance))
					{
						auto end = instance.seq.back() + 1;
						for (auto it = instance.seq.front(); it != end; ++it)
						{
							int64_t idx = it.GetIndex();
							int64_t maxidx = storage_.GetChrVerticesCount(it.GetChrId());							
							blockId_[it.GetChrId()][it.GetIndex()].block = it.IsPositiveStrand() ? blocksFound_ : -blocksFound_;
							blockId_[it.GetChrId()][it.GetIndex()].instance = instanceCount;
						}

						instanceCount++;
					}					
				}
			}
		}

		void ExtendPathForward(Path & currentPath, Path & bestPath, int maxDepth)
		{
			if (maxDepth > 0)
			{
				std::vector<Edge> adjList;
				int64_t prevVertex = currentPath.PathBody().back().vertex;
				storage_.OutgoingEdges(prevVertex, adjList);
				for (auto e : adjList)
				{
					if (forbidden_.count(e.GetLightEdge()) == 0)
					{
						if (currentPath.PointPushBack(e))
						{
#ifdef _DEBUG_OUT
							currentPath.DebugOut(std::cerr);
#endif
							int64_t currentScore = currentPath.Score(scoreFullChains_);
							int64_t prevBestScore = bestPath.Score(scoreFullChains_);
							if (currentScore > prevBestScore && currentPath.Instances().size() > 1)
							{
								bestPath = currentPath;
							}

							ExtendPathForward(currentPath, bestPath, maxDepth - 1);
							currentPath.PointPopBack();
						}
					}
				}
			}
		}

		void ExtendPathBackward(Path & currentPath, Path & bestPath, int maxDepth)
		{
			if (maxDepth > 0)
			{
				std::vector<Edge> adjList;
				int64_t prevVertex = currentPath.PathBody().front().vertex;
				storage_.IngoingEdges(prevVertex, adjList);
				for (auto e : adjList)
				{
					if (forbidden_.count(e.GetLightEdge()) == 0)
					{
						if (currentPath.PointPushFront(e))
						{
#ifdef _DEBUG_OUT
							currentPath.DebugOut(std::cerr);
#endif
							if (currentPath.Score(scoreFullChains_) > bestPath.Score(scoreFullChains_) && currentPath.Instances().size() > 1)
							{
								bestPath = currentPath;
							}

							ExtendPathBackward(currentPath, bestPath, maxDepth - 1);
							currentPath.PointPopFront();
						}
					}
				}
			}
		}


		/*
		double EdgeWeight(const std::map<EdgeStorage::Edge, uint32_t> & bubbleCount, EdgeStorage::Edge e) const
		{
			if (bubbleCount.count(e) > 0)
			{
				return bubbleCount.find(e)->second;
			}

			if (bubbleCount.count(e.Reverse()) > 0)
			{
				return bubbleCount.find(e.Reverse())->second;
			}

			return .5;
		}
		
		bool RandomChoice(Path & currentPath, std::map<EdgeStorage::Edge, int64_t> & edgeLength, const std::map<EdgeStorage::Edge, uint32_t> & bubbleCount)
		{
			std::vector<bool> isForward;
			std::vector<double> probability;
			std::vector<double> weight;
			std::vector<int64_t> nextVertex;
			std::vector<int64_t> adjForwardList;
			std::vector<int64_t> adjBackwardList;
			storage_.SuccessorsList(currentPath.vertex.back(), adjForwardList);
			storage_.PredecessorsList(currentPath.vertex.front(), adjBackwardList);
			for (int64_t v : adjForwardList)
			{
				EdgeStorage::Edge e(currentPath.vertex.back(), v);
				if (!forbidden_.count(e) && !forbidden_.count(e.Reverse()) && 
					std::find(currentPath.vertex.begin(), currentPath.vertex.end(), v) == currentPath.vertex.end())
				{
					nextVertex.push_back(v);
					isForward.push_back(true);
					weight.push_back(EdgeWeigth(bubbleCount, e));
				}
			}

			for (int64_t v : adjBackwardList)
			{
				EdgeStorage::Edge e(v, currentPath.vertex.front());
				if (!forbidden_.count(e) && !forbidden_.count(e.Reverse()) && 
					std::find(currentPath.vertex.begin(), currentPath.vertex.end(), v) == currentPath.vertex.end())
				{
					nextVertex.push_back(v);
					isForward.push_back(false);
					weight.push_back(EdgeWeigth(bubbleCount, e));
				}
			}

			if (weight.size() > 0)
			{
				double total = std::accumulate(weight.begin(), weight.end(), 0.0);
				for (size_t i = 0; i < weight.size(); i++)
				{
					probability.push_back(weight[i] / total + (i > 0 ? probability[i - 1] : 0));
				}

				probability.back() = 1.01;
				double coin = double(rand()) / RAND_MAX;
				size_t it = std::lower_bound(probability.begin(), probability.end(), coin) - probability.begin();
				if (isForward[it])
				{
					currentPath.distance.push_back(currentPath.distance.back() + edgeLength[EdgeStorage::Edge(currentPath.vertex.back(), nextVertex[it])]);
					currentPath.vertex.push_back(nextVertex[it]);
				}
				else
				{
					currentPath.distance.push_front(currentPath.distance.front() -edgeLength[EdgeStorage::Edge(nextVertex[it], currentPath.vertex.front())]);
					currentPath.vertex.push_front(nextVertex[it]);
				}

				return true;
			}			

			return false;
		}

		void ExtendPathRandom(Path & startPath, Path & bestPath, std::map<EdgeStorage::Edge, int64_t> & edgeLength, int sampleSize, int maxDepth, const std::map<EdgeStorage::Edge, uint32_t> & bubbleCount)		
		{
			std::map<std::pair<uint64_t, uint64_t>, bool> seen;
			for (size_t it = 0; it < sampleSize; it++)
			{
				Path currentPath = startPath;
				for (size_t i = 0; i < maxDepth; i++)
				{
					if (RandomChoice(currentPath, edgeLength, bubbleCount))
					{
						seen.clear();
						RescorePath(currentPath, seen);
						if (currentPath.score > bestPath.score)
						{
							bestPath = currentPath;
						}
					}
					else
					{
						break;
					}
				}
			}						
		}*/		

		void CountBubbles(int64_t vertexId, std::map<int64_t, int64_t> & bubbleCount)
		{
			BubbledBranches bulges;
			std::map<int64_t, BranchData> visit;
			std::vector<JunctionStorage::JunctionIterator> instance;
			for (size_t i = 0; i < storage_.GetInstancesCount(vertexId); i++)
			{				
				JunctionStorage::JunctionIterator vertex = storage_.GetJunctionInstance(vertexId, i);
				instance.push_back(vertex);				
				for (int64_t startPosition = vertex++.GetPosition(); vertex.Valid() && abs(startPosition - vertex.GetPosition()) < maxBranchSize_; ++vertex)
				{					
					int64_t nowVertexId = vertex.GetVertexId();
					auto point = visit.find(nowVertexId);
					if (point == visit.end())
					{
						BranchData bData;
						bData.branchId.push_back(i);
						visit[nowVertexId] = bData;
					}
					else
					{
						point->second.branchId.push_back(i);
						break;
					}
				}
			}

			for (auto point = visit.begin(); point != visit.end(); ++point)
			{
				if (point->second.branchId.size() > 1)
				{
					size_t n = point->second.branchId.size();
					for (size_t i = 0; i < point->second.branchId.size(); ++i)
					{
						auto & edgeIt = instance[point->second.branchId[i]];
						if (edgeIt.IsPositiveStrand())
						{
							bubbleCount[edgeIt.GetVertexId()] += n * (n - 1) / 2;
						}
					}
				}
			}
		}

		

		int64_t k_;
		bool scoreFullChains_;
		int64_t lookingDepth_;
		int64_t blocksFound_;
		int64_t minBlockSize_;
		int64_t maxBranchSize_;
		int64_t flankingThreshold_;		
		const JunctionStorage & storage_;
		std::set<LightEdge> forbidden_;
		std::vector<std::vector<Assignment> > blockId_;
		std::vector<std::vector<int64_t> > syntenyPath_;		

	};
}

#endif