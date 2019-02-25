#ifndef _TRASERVAL_H_
#define _TRAVERSAL_H_

//#define _DEBUG_OUT_

#include <set>
#include <map>
#include <list>
#include <ctime>
#include <queue>
#include <iterator>
#include <cassert>
#include <numeric>
#include <sstream>
#include <iostream>
#include <functional>
#include <unordered_map>

#include <tbb/parallel_for.h>

#include "path.h"

namespace Sibelia
{
	extern const std::string DELIMITER;
	extern const std::string VERSION;

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

		BlocksFinder(JunctionStorage & storage, size_t k) : storage_(storage), k_(k)
		{
			progressCount_ = 50;
			scoreFullChains_ = true;			
		}

		struct ProcessVertex
		{
		public:
			BlocksFinder & finder;
			std::vector<int64_t> & shuffle;

			ProcessVertex(BlocksFinder & finder, std::vector<int64_t> & shuffle) : finder(finder), shuffle(shuffle)
			{
			}

			void operator()(tbb::blocked_range<size_t> & range) const
			{
				std::vector<size_t> data;
				std::vector<uint32_t> count(finder.storage_.GetVerticesNumber() * 2 + 1, 0);
				std::pair<int64_t, std::vector<Path::Instance> > goodInstance;
				Path finalizer(finder.storage_, finder.maxBranchSize_, finder.minBlockSize_, finder.minBlockSize_, finder.maxFlankingSize_);
				Path currentPath(finder.storage_, finder.maxBranchSize_, finder.minBlockSize_, finder.minBlockSize_, finder.maxFlankingSize_);
				for (size_t i = range.begin(); i != range.end(); i++)
				{
					if (finder.count_++ % finder.progressPortion_ == 0)
					{
						finder.progressMutex_.lock();
						std::cout << '.' << std::flush;
						finder.progressMutex_.unlock();
					}

					int64_t score;
					int64_t vid = shuffle[i];
#ifdef _DEBUG_OUT_
					finder.debug_ = finder.missingVertex_.count(vid);
					if (finder.debug_)
					{
						std::cerr << "Vid: " << vid << std::endl;
					}
#endif
					for (bool explore = true; explore;)
					{
						currentPath.Init(vid);
						if (currentPath.AllInstances().size() < 2)
						{
							currentPath.Clear();
							break;
						}

						int64_t bestScore = 0;
						size_t bestRightSize = currentPath.RightSize();
						size_t bestLeftSize = currentPath.LeftSize();
#ifdef _DEBUG_OUT_
						if (finder.debug_)
						{
							std::cerr << "Going forward:" << std::endl;
						}
#endif
						int64_t minRun = max(finder.minBlockSize_, finder.maxBranchSize_) * 2;
						while (true)
						{
							bool ret = true;
							bool positive = false;
							int64_t prevLength = currentPath.MiddlePathLength();
							while ((ret = finder.ExtendPathForward(currentPath, count, data, bestRightSize, bestScore, score)) && currentPath.MiddlePathLength() - prevLength <= minRun)
							{
								positive = positive || (score > 0);
							}

							if (!ret || !positive)
							{
								break;
							}
						}

						{
							std::vector<Edge> bestEdge;
							for (size_t i = 0; i < bestRightSize - 1; i++)
							{
								bestEdge.push_back(currentPath.RightPoint(i).GetEdge());
							}

							currentPath.Clear();
							currentPath.Init(vid);
							for (auto & e : bestEdge)
							{
								currentPath.PointPushBack(e);
							}
						}
#ifdef _DEBUG_OUT_
						if (finder.debug_)
						{
							std::cerr << "Going backward:" << std::endl;
						}
#endif
						while (true)
						{
							bool ret = true;
							bool positive = false;
							int64_t prevLength = currentPath.MiddlePathLength();
							while ((ret = finder.ExtendPathBackward(currentPath, count, data, bestLeftSize, bestScore, score)) && currentPath.MiddlePathLength() - prevLength <= minRun);
							{
								positive = positive || (score > 0);
							}

							if (!ret || !positive)
							{
								break;
							}
						}

						if (bestScore > 0)
						{
#ifdef _DEBUG_OUT_
							if (finder.debug_)
							{
								std::cerr << "Setting a new block. Best score:" << bestScore << std::endl;
								currentPath.DumpPath(std::cerr);
								currentPath.DumpInstances(std::cerr);
							}
#endif
							if (!finder.TryFinalizeBlock(currentPath, finalizer, bestRightSize, bestLeftSize))
							{
								explore = false;
							}
						}
						else
						{
							explore = false;
						}

						currentPath.Clear();
					}
				}
			}
		};

		static bool DegreeCompare(const JunctionStorage & storage, int64_t v1, int64_t v2)
		{
			return storage.GetInstancesCount(v1) > storage.GetInstancesCount(v2);
		}

		void Split(std::string & source, std::vector<std::string> & result)
		{
			std::stringstream ss;
			ss << source;
			result.clear();
			while (ss >> source)
			{
				result.push_back(source);
			}
		}

		void FindBlocks(int64_t minBlockSize, int64_t maxBranchSize, int64_t maxFlankingSize, int64_t lookingDepth, int64_t sampleSize, int64_t threads, const std::string & debugOut)
		{
			blocksFound_ = 0;
			sampleSize_ = sampleSize;
			lookingDepth_ = lookingDepth;
			minBlockSize_ = minBlockSize;
			maxBranchSize_ = maxBranchSize;
			maxFlankingSize_ = maxFlankingSize;
			blockId_.resize(storage_.GetChrNumber());
			for (int64_t i = 0; i < storage_.GetChrNumber(); i++)
			{
				blockId_[i].resize(storage_.GetChrVerticesCount(i));
			}

			std::vector<int64_t> shuffle;
			for (int64_t v = -storage_.GetVerticesNumber() + 1; v < storage_.GetVerticesNumber(); v++)
			{
				for (JunctionStorage::JunctionIterator it(v); it.Valid(); ++it)
				{
					if (it.IsPositiveStrand())
					{
						shuffle.push_back(v);
						break;
					}
				}
			}

			using namespace std::placeholders;
			tbb::task_scheduler_init init(static_cast<int>(threads));
			std::random_shuffle(shuffle.begin(), shuffle.end());

			time_t mark = time(0);
			count_ = 0;
			/*
			std::cout << '[' << std::flush;
			progressPortion_ = shuffle.size() / progressCount_;
		
			tbb::parallel_for(tbb::blocked_range<size_t>(0, shuffle.size()), ProcessVertex(*this, shuffle));
			std::cout << ']' << std::endl;
			*/
			starter_ = 0;
			tbb::parallel_for(tbb::blocked_range<size_t>(0, shuffle.size()), CheckIfSource(*this, shuffle));
			//std::cout << "Time: " << time(0) - mark << std::endl;
		}

		
		void ListBlocksSequences(const BlockList & block, const std::string & directory) const
		{
			std::vector<IndexPair> group;
			BlockList blockList = block;
			GroupBy(blockList, compareById, std::back_inserter(group));
			for (std::vector<IndexPair>::iterator it = group.begin(); it != group.end(); ++it)
			{
				std::ofstream out;
				std::stringstream ss;
				ss << directory << "/" << blockList[it->first].GetBlockId() << ".fa";
				TryOpenFile(ss.str(), out);
				for (size_t block = it->first; block < it->second; block++)
				{
					size_t length = blockList[block].GetLength();
					size_t chr = blockList[block].GetChrId();
					size_t chrSize = storage_.GetChrSequence(chr).size();
					out << ">" << blockList[block].GetBlockId() << "_" << block - it->first << " ";
					out << storage_.GetChrDescription(chr) << ";";
					if (blockList[block].GetSignedBlockId() > 0)
					{
						out << blockList[block].GetStart() << ";" << length << ";" << "+;" << chrSize << std::endl;
						OutputLines(storage_.GetChrSequence(chr).begin() + blockList[block].GetStart(), length, out);
					}
					else
					{
						size_t start = chrSize - blockList[block].GetEnd();
						out << start << ";" << length << ";" << "-;" << chrSize << std::endl;
						std::string::const_reverse_iterator it(storage_.GetChrSequence(chr).begin() + blockList[block].GetEnd());
						OutputLines(CFancyIterator(it, TwoPaCo::DnaChar::ReverseChar, ' '), length, out);
					}

					out << std::endl;
				}
			}
		}


		void GenerateOutput(const std::string & outDir, bool genSeq) const
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
					if (storage_.GetIterator(chr, i).IsUsed())
					{
						int64_t bid = blockId_[chr][i].block;
						size_t j = i;
						for (; j < blockId_[chr].size() && blockId_[chr][i] == blockId_[chr][j]; j++);
						j--;
						int64_t start = storage_.GetIterator(chr, i, bid > 0).GetPosition() + (bid > 0 ? 0 : -k_);
						int64_t end = storage_.GetIterator(chr, j, bid > 0).GetPosition() + (bid > 0 ? k_ : 0);
						instance.push_back(BlockInstance(int(bid), chr, size_t(start), size_t(end)));
						i = j + 1;
					}
					else
					{
						++i;
					}
				}
			}

			std::cout.setf(std::cout.fixed);
			std::cout.precision(2);
			std::cout << "Blocks found: " << blocksFound_ << std::endl;
			std::cout << "Total coverage: " << CalculateCoverage(instance) << std::endl;
			std::cout << "Starters: " << starter_ << std::endl;

			CreateOutDirectory(outDir);
			std::string blocksDir = outDir + "/blocks";
			ListBlocksIndicesGFF(instance, outDir + "/" + "blocks_coords.gff");
			if (genSeq)
			{
				CreateOutDirectory(blocksDir);
				ListBlocksSequences(instance, blocksDir);
			}

	//		GenerateReport(instance, outDir + "/" + "coverage_report.txt");*/
	//		ListBlocksIndices(instance, outDir + "/" + "blocks_coords.txt");
		}


	private:

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

		double CalculateCoverage(const BlockList & block) const;
		void GenerateReport(const BlockList & block, const std::string & fileName) const;
		void CalculateCoverage(GroupedBlockList::const_iterator start, GroupedBlockList::const_iterator end, std::vector<double> & ret) const;
		std::string OutputIndex(const BlockInstance & block) const;
		void OutputBlocks(const std::vector<BlockInstance>& block, std::ofstream& out) const;
		void ListBlocksIndices(const BlockList & block, const std::string & fileName) const;
		void ListBlocksIndicesGFF(const BlockList & blockList, const std::string & fileName) const;
		void ListChromosomesAsPermutations(const BlockList & block, const std::string & fileName) const;
		void TryOpenFile(const std::string & fileName, std::ofstream & stream) const;
		void ListChrs(std::ostream & out) const;

		template<class T>
		void DumpVertex(int64_t id, std::ostream & out, T & visit, int64_t cnt = 5) const
		{
			for (auto kt = JunctionStorage::JunctionIterator(id); kt.Valid(); ++kt)
			{
				auto jt = kt.SequentialIterator();
				for (int64_t i = 0; i < cnt; i++)
				{
					auto it = jt - 1;
					auto pr = std::make_pair(it, jt);
					if (it.Valid() && std::find(visit.begin(), visit.end(), pr) == visit.end())
					{
						int64_t length = it.GetPosition() - jt.GetPosition();
						out << it.GetVertexId() << " -> " << jt.GetVertexId()
							<< "[label=\"" << it.GetChar() << ", " << it.GetChrId() << ", " << it.GetPosition() << "," << length << "\""
							<< (it.IsPositiveStrand() ? "color=blue" : "color=red") << "]\n";
						visit.push_back(pr);
					}

					jt = it;
				}
			}

			for (auto kt = JunctionStorage::JunctionIterator(id); kt.Valid(); ++kt)
			{
				auto it = kt.SequentialIterator();
				for (int64_t i = 0; i < cnt; i++)
				{
					auto jt = it + 1;
					auto pr = std::make_pair(it, jt);
					if (jt.Valid() && std::find(visit.begin(), visit.end(), pr) == visit.end())
					{
						int64_t length = it.GetPosition() - jt.GetPosition();
						out << it.GetVertexId() << " -> " << jt.GetVertexId()
							<< "[label=\"" << it.GetChar() << ", " << it.GetChrId() << ", " << it.GetPosition() << "," << length << "\""
							<< (it.IsPositiveStrand() ? "color=blue" : "color=red") << "]\n";
						visit.push_back(pr);
					}

					it = jt;
				}
			}
		}
		
		struct BranchData
		{
			std::vector<size_t> branchId;
		};

		typedef std::vector<std::vector<size_t> > BubbledBranches;

		struct Fork
		{
			Fork(JunctionStorage::JunctionSequentialIterator it, JunctionStorage::JunctionSequentialIterator jt)
			{
				if (it < jt)
				{
					branch[0] = it;
					branch[1] = jt;
				}
				else
				{
					branch[0] = jt;
					branch[1] = it;
				}
			}

			JunctionStorage::JunctionSequentialIterator branch[2];
		};

		int64_t ChainLength(const Fork & now, const Fork & next) const
		{
			return min(abs(now.branch[0].GetPosition() - next.branch[0].GetPosition()), abs(now.branch[1].GetPosition() - next.branch[1].GetPosition()));
		}
		
		Fork ExpandSourceFork(const Fork & source) const
		{
			for (auto now = source; ; )
			{
				auto next = TakeBubbleStep(now);
				if (next.branch[0].Valid())
				{
					int64_t vid0 = now.branch[0].GetVertexId();
					int64_t vid1 = now.branch[1].GetVertexId();
					assert(vid0 == vid1 && abs(now.branch[0].GetPosition() - next.branch[0].GetPosition()) < maxBranchSize_ &&
						abs(now.branch[1].GetPosition() - next.branch[1].GetPosition()) < maxBranchSize_);
					now = next;
				}
				else
				{
					return now;
				}
			}
		}


		Fork TakeBubbleStep(const Fork & source) const
		{
			if (source.branch[0].GetChar() == source.branch[1].GetChar() && (source.branch[0] + 1).GetVertexId() == (source.branch[1] + 1).GetVertexId())
			{
				return Fork(source.branch[0] + 1, source.branch[1] + 1);
			}

			auto it = source.branch[0];
			std::map<int64_t, int64_t> firstBranch;
			for (int64_t i = 1; abs(it.GetPosition() - source.branch[0].GetPosition()) < maxBranchSize_ && (++it).Valid(); i++)
			{
				int64_t d = abs(it.GetPosition() - source.branch[0].GetPosition());
				firstBranch[it.GetVertexId()] = i;
			}

			it = source.branch[1];
			for (int64_t i = 1; abs(it.GetPosition() - source.branch[1].GetPosition()) < maxBranchSize_ && (++it).Valid(); i++)
			{
				auto kt = firstBranch.find(it.GetVertexId());
				if (kt != firstBranch.end())
				{
					return Fork(source.branch[0] + kt->second, it);
				}
			}

			return Fork(JunctionStorage::JunctionSequentialIterator(), JunctionStorage::JunctionSequentialIterator());
		}

		void BubbledBranchesForward(int64_t vertexId, const std::vector<JunctionStorage::JunctionSequentialIterator> & instance, BubbledBranches & bulges) const
		{
			std::vector<size_t> parallelEdge[5];
			std::map<int64_t, BranchData> visit;
			bulges.assign(instance.size(), std::vector<size_t>());
			for (size_t i = 0; i < instance.size(); i++)
			{
				auto vertex = instance[i];
				if ((vertex + 1).Valid())
				{
					parallelEdge[TwoPaCo::DnaChar::MakeUpChar(vertex.GetChar())].push_back(i);
				}

				for (int64_t startPosition = vertex++.GetPosition(); vertex.Valid() && abs(startPosition - vertex.GetPosition()) <= maxBranchSize_; ++vertex)
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
					}
				}
			}

			for (size_t i = 0; i < 5; i++)
			{
				for (size_t j = 0; j < parallelEdge[i].size(); j++)
				{
					for (size_t k = j + 1; k < parallelEdge[i].size(); k++)
					{
						size_t smallBranch = parallelEdge[i][j];
						size_t largeBranch = parallelEdge[i][k];
						bulges[smallBranch].push_back(largeBranch);
					}
				}
			}

			for (auto point = visit.begin(); point != visit.end(); ++point)
			{
				std::sort(point->second.branchId.begin(), point->second.branchId.end());
				for (size_t j = 0; j < point->second.branchId.size(); j++)
				{
					for (size_t k = j + 1; k < point->second.branchId.size(); k++)
					{
						size_t smallBranch = point->second.branchId[j];
						size_t largeBranch = point->second.branchId[k];
						if (smallBranch != largeBranch && std::find(bulges[smallBranch].begin(), bulges[smallBranch].end(), largeBranch) == bulges[smallBranch].end())
						{
							bulges[smallBranch].push_back(largeBranch);
						}
					}
				}
			}
		}

		void BubbledBranchesBackward(int64_t vertexId, const std::vector<JunctionStorage::JunctionSequentialIterator> & instance, BubbledBranches & bulges) const
		{
			std::vector<size_t> parallelEdge[5];
			std::map<int64_t, BranchData> visit;
			bulges.assign(instance.size(), std::vector<size_t>());
			for (size_t i = 0; i < instance.size(); i++)
			{
				auto vertex = instance[i];
				auto prev = vertex - 1;
				if (prev.Valid())
				{
					parallelEdge[TwoPaCo::DnaChar::MakeUpChar(prev.GetChar())].push_back(i);
				}

				for (int64_t startPosition = vertex--.GetPosition(); vertex.Valid() && abs(startPosition - vertex.GetPosition()) <= maxBranchSize_; --vertex)
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
					}
				}
			}

			for (size_t i = 0; i < 5; i++)
			{
				for (size_t j = 0; j < parallelEdge[i].size(); j++)
				{
					for (size_t k = j + 1; k < parallelEdge[i].size(); k++)
					{
						size_t smallBranch = parallelEdge[i][j];
						size_t largeBranch = parallelEdge[i][k];
						bulges[smallBranch].push_back(largeBranch);
					}
				}
			}

			for (auto point = visit.begin(); point != visit.end(); ++point)
			{
				std::sort(point->second.branchId.begin(), point->second.branchId.end());
				for (size_t j = 0; j < point->second.branchId.size(); j++)
				{
					for (size_t k = j + 1; k < point->second.branchId.size(); k++)
					{
						size_t smallBranch = point->second.branchId[j];
						size_t largeBranch = point->second.branchId[k];
						if (smallBranch != largeBranch && std::find(bulges[smallBranch].begin(), bulges[smallBranch].end(), largeBranch) == bulges[smallBranch].end())
						{
							bulges[smallBranch].push_back(largeBranch);
						}
					}
				}
			}
		}

		struct CheckIfSource
		{
		public:
			BlocksFinder & finder;
			std::vector<int64_t> & shuffle;

			CheckIfSource(BlocksFinder & finder, std::vector<int64_t> & shuffle) : finder(finder), shuffle(shuffle)
			{
			}

			void operator()(tbb::blocked_range<size_t> & range) const
			{
				std::vector<uint32_t> data;
				std::vector<uint32_t> count(finder.storage_.GetVerticesNumber() * 2 + 1, 0);
				std::pair<int64_t, std::vector<Path::Instance> > goodInstance;
				Path finalizer(finder.storage_, finder.maxBranchSize_, finder.minBlockSize_, finder.minBlockSize_, finder.maxFlankingSize_);
				Path currentPath(finder.storage_, finder.maxBranchSize_, finder.minBlockSize_, finder.minBlockSize_, finder.maxFlankingSize_);

				std::vector<char> isStarter;
				BubbledBranches forwardBubble;
				BubbledBranches backwardBubble;
				std::vector<JunctionStorage::JunctionSequentialIterator> instance;
				for (size_t r = range.begin(); r != range.end(); r++)
				{
					if (finder.count_++ % 10000 == 0)
					{
						tbb::mutex::scoped_lock lock(finder.globalMutex_);
						std::cout << finder.count_ << '\t' << shuffle.size() << std::endl;
					}

					instance.clear();
					int64_t vertex = shuffle[r];
					for (auto it = JunctionStorage::JunctionIterator(vertex); it.Valid(); ++it)
					{
						instance.push_back(it.SequentialIterator());
					}

					bool nowStarter = false;
					isStarter.assign(instance.size(), false);
					finder.BubbledBranchesForward(vertex, instance, forwardBubble);
					finder.BubbledBranchesBackward(vertex, instance, backwardBubble);
					for (size_t i = 0; i < forwardBubble.size() && !nowStarter; i++)
					{
						for (size_t j = 0; j < forwardBubble[i].size(); j++)
						{
							size_t k = forwardBubble[i][j];
							if (std::find(backwardBubble[i].begin(), backwardBubble[i].end(), k) == backwardBubble[i].end())
							{
								isStarter[i] = isStarter[j] = true;
								nowStarter = true;
								finder.starter_++;
								break;
							}
						}
					}

					
				}
			}

			void Explore(Path & currentPath, Path & finalizer, int64_t vid, std::vector<uint32_t> & data, std::vector<uint32_t> & count, JunctionStorage::JunctionSequentialIterator templateIt) const
			{
				int64_t score;
				currentPath.Init(vid);
				int64_t minRun = max(finder.minBlockSize_, finder.maxBranchSize_) * 5;
				if (currentPath.AllInstances().size() < 2)
				{
					currentPath.Clear();
					return;
				}

				size_t rightRun = 0;
				int64_t bestScore = 0;
				size_t bestLeftSize = 0;
				size_t bestRightSize = 0;
				auto startIt = templateIt;

				while (true)
				{
					bool ret = true;
					bool positive = false;
					int64_t prevLength = currentPath.MiddlePathLength();
					for (; (ret = ((templateIt + 1).Valid() && currentPath.PointPushBack(templateIt.OutgoingEdge()))) && currentPath.MiddlePathLength() - prevLength <= minRun; ++templateIt)
					{
						int64_t score = currentPath.Score();
						positive = positive || (score > 0);
						if (score > bestScore)
						{
							bestScore = score;
							rightRun = currentPath.RightDistance();
							bestRightSize = currentPath.RightSize();
						}
					}

					if (!ret || !positive)
					{
						break;
					}
				}


				if (bestRightSize > 0)
				{
					std::vector<Edge> bestEdge;
					for (size_t i = 0; i < bestRightSize - 1; i++)
					{
						bestEdge.push_back(currentPath.RightPoint(i).GetEdge());
					}

					currentPath.Clear();
					currentPath.Init(vid);
					for (auto & e : bestEdge)
					{
						currentPath.PointPushBack(e);
					}
				}
				else
				{
					currentPath.Clear();
					currentPath.Init(vid);
				}

				size_t leftRun = 0;
				templateIt = startIt;

				while (true)
				{
					bool ret = true;
					bool positive = false;
					int64_t prevLength = currentPath.MiddlePathLength();

					for (; (ret = (templateIt - 1).Valid() && currentPath.PointPushFront(templateIt.IngoingEdge())) && currentPath.MiddlePathLength() - prevLength <= minRun; --templateIt)
					{
						int64_t score = currentPath.Score();
						positive = positive || (score > 0);
						if (score > bestScore)
						{
							bestScore = score;
							leftRun = currentPath.LeftDistance();
							bestLeftSize = currentPath.LeftSize();
						}
					}

					if (!ret || !positive)
					{
						break;
					}
				}

				if (bestScore > 0)
				{
					finder.TryFinalizeBlock(currentPath, finalizer, bestRightSize, bestLeftSize);
				}

				currentPath.Clear();
			}
		};

		static void AddIfNotExists(std::vector<int64_t> & adj, int64_t value)
		{
			if (std::find(adj.begin(), adj.end(), value) == adj.end())
			{
				adj.push_back(value);
			}
		}


		bool TryFinalizeBlock(const Path & currentPath, Path & finalizer, size_t bestRightSize, size_t bestLeftSize)
		{
			bool ret = false;
			std::vector<Path::InstanceSet::const_iterator> lockInstance;
			for (auto it : currentPath.GoodInstancesList())
			{
				lockInstance.push_back(it);
			}
			
			std::sort(lockInstance.begin(), lockInstance.end(), Path::CmpInstance);
			{
				std::pair<size_t, size_t> idx(SIZE_MAX, SIZE_MAX);
				for (auto & instance : lockInstance)
				{
					if (instance->Front().IsPositiveStrand())
					{
						storage_.LockRange(instance->Front(), instance->Back(), idx);
					}
					else
					{
						storage_.LockRange(instance->Back().Reverse(), instance->Front().Reverse(), idx);
					}
				}
			}
		
			finalizer.Init(currentPath.Origin());
			for (size_t i = 0; i < bestRightSize - 1 && finalizer.PointPushBack(currentPath.RightPoint(i).GetEdge()); i++);
			for (size_t i = 0; i < bestLeftSize - 1 && finalizer.PointPushFront(currentPath.LeftPoint(i).GetEdge()); i++);
			if (finalizer.Score() > 0 && finalizer.GoodInstances() > 1)
			{
				ret = true;
				int64_t instanceCount = 0;
				int64_t currentBlock = ++blocksFound_;	
				for (auto jt : finalizer.AllInstances())
				{
					if (finalizer.IsGoodInstance(*jt))
					{
						auto it = jt->Front();
						do
						{
							it.MarkUsed();							
							int64_t idx = it.GetIndex();
							int64_t maxidx = storage_.GetChrVerticesCount(it.GetChrId());
							blockId_[it.GetChrId()][it.GetIndex()].block = int32_t(it.IsPositiveStrand() ? +currentBlock : -currentBlock);
							blockId_[it.GetChrId()][it.GetIndex()].instance = int32_t(instanceCount);

						} while (it++ != jt->Back());

						instanceCount++;
					}
				}
			}
				
			finalizer.Clear();
			std::pair<size_t, size_t> idx(SIZE_MAX, SIZE_MAX);
			for (auto & instance : lockInstance)
			{
				if (instance->Front().IsPositiveStrand())
				{
					storage_.UnlockRange(instance->Front(), instance->Back(), idx);
				}
				else
				{
					storage_.UnlockRange(instance->Back().Reverse(), instance->Front().Reverse(), idx);
				}
			}

			return ret;
		}

		struct NextVertex
		{
			int64_t diff;
			int64_t count;
			JunctionStorage::JunctionSequentialIterator origin;
			NextVertex() : count(0)
			{

			}

			NextVertex(int64_t diff, JunctionStorage::JunctionSequentialIterator origin) : origin(origin), diff(diff), count(1)
			{

			}
		};

		std::pair<int64_t, NextVertex> MostPopularVertex(const Path & currentPath, bool forward, std::vector<uint32_t> & count, std::vector<size_t> & data)
		{
			NextVertex ret;
			int64_t bestVid = 0;
			int64_t startVid = forward ? currentPath.RightVertex() : currentPath.LeftVertex();
			const auto & instList = currentPath.GoodInstancesList().size() >= 2 ? currentPath.GoodInstancesList() : currentPath.AllInstances();
			for (auto & inst : instList)
			{
				int64_t nowVid = forward ? inst->Back().GetVertexId() : inst->Front().GetVertexId();
				if (nowVid == startVid)
				{
					int64_t weight = abs(inst->Front().GetPosition() - inst->Back().GetPosition()) + 1;
					auto origin = forward ? inst->Back() : inst->Front();
					auto it = forward ? origin.Next() : origin.Prev();
					for (size_t d = 1; it.Valid() && (d < size_t(lookingDepth_)  || abs(it.GetPosition() - origin.GetPosition()) <= maxBranchSize_); d++)
					{
						int64_t vid = it.GetVertexId();
						if (!currentPath.IsInPath(vid) && !it.IsUsed())
						{
							auto adjVid = vid + storage_.GetVerticesNumber();
							if (count[adjVid] == 0)
							{
								data.push_back(adjVid);
							}

							count[adjVid] += static_cast<uint32_t>(weight);
							auto diff = abs(it.GetAbsolutePosition() - origin.GetAbsolutePosition());
							if (count[adjVid] > ret.count || (count[adjVid] == ret.count && diff < ret.diff))
							{
								ret.diff = diff;
								ret.origin = origin;
								ret.count = count[adjVid];
								bestVid = vid;
							}
						}
						else
						{
							break;
						}

						if (forward)
						{
							++it;
						}
						else
						{
							--it;
						}
					}
				}
			}


			for (auto vid : data)
			{
				count[vid] = 0;
			}

			data.clear();
			return std::make_pair(bestVid, ret);
		}

		bool ExtendPathForward(Path & currentPath,
			std::vector<uint32_t> & count,
			std::vector<size_t> & data,
			size_t & bestRightSize,
			int64_t & bestScore,
			int64_t & nowScore)
		{
			bool success = false;
			int64_t origin = currentPath.Origin();
			std::pair<int64_t, NextVertex> nextForwardVid;
			nextForwardVid = MostPopularVertex(currentPath, true, count, data);
			if (nextForwardVid.first != 0)
			{
				for (auto it = nextForwardVid.second.origin; it.GetVertexId() != nextForwardVid.first; ++it)
				{
#ifdef _DEBUG_OUT_
					if (debug_)
					{
						std::cerr << "Attempting to push back the vertex:" << it.GetVertexId() << std::endl;
					}

					if (missingVertex_.count(it.GetVertexId()))
					{
						std::cerr << "Alert: " << it.GetVertexId() << ", origin: " << currentPath.Origin() << std::endl;
					}
#endif
					success = currentPath.PointPushBack(it.OutgoingEdge());
					if (success)
					{
						nowScore = currentPath.Score(scoreFullChains_);
#ifdef _DEBUG_OUT_
						if (debug_)
						{
							std::cerr << "Success! New score:" << nowScore << std::endl;
							currentPath.DumpPath(std::cerr);
							currentPath.DumpInstances(std::cerr);
						}
#endif												
						if (nowScore > bestScore)
						{
							bestScore = nowScore;
							bestRightSize = currentPath.RightSize();
						}
					}
				}
			}

			return success;
		}

		bool ExtendPathBackward(Path & currentPath,
			std::vector<uint32_t> & count,
			std::vector<size_t> & data,
			size_t & bestLeftSize,
			int64_t & bestScore,
			int64_t & nowScore)
		{
			bool success = false;
			std::pair<int64_t, NextVertex> nextBackwardVid;
			nextBackwardVid = MostPopularVertex(currentPath, false, count, data);
			if (nextBackwardVid.first != 0)
			{
				for (auto it = nextBackwardVid.second.origin; it.GetVertexId() != nextBackwardVid.first; --it)
				{
#ifdef _DEBUG_OUT_
					if (debug_)
					{
						std::cerr << "Attempting to push front the vertex:" << it.GetVertexId() << std::endl;
					}

					if (missingVertex_.count(it.GetVertexId()))
					{
						std::cerr << "Alert: " << it.GetVertexId() << ", origin: " << currentPath.Origin() << std::endl;
					}
#endif
					success = currentPath.PointPushFront(it.IngoingEdge());
					if (success)
					{
						nowScore = currentPath.Score(scoreFullChains_);
#ifdef _DEBUG_OUT_
						if (debug_)
						{
							std::cerr << "Success! New score:" << nowScore << std::endl;
							currentPath.DumpPath(std::cerr);
							currentPath.DumpInstances(std::cerr);
						}
#endif		
						if (nowScore > bestScore)
						{
							bestScore = nowScore;
							bestLeftSize = currentPath.LeftSize();
						}
					}
				}
			}

			return success;
		}

		int64_t k_;
		size_t progressCount_;
		size_t progressPortion_;
		std::atomic<int64_t> count_;
		std::atomic<int64_t> starter_;
		std::atomic<int64_t> blocksFound_;
		int64_t sampleSize_;
		int64_t scalingFactor_;
		bool scoreFullChains_;
		int64_t lookingDepth_;
		int64_t minBlockSize_;
		int64_t maxBranchSize_;
		int64_t maxFlankingSize_;
		JunctionStorage & storage_;
		tbb::mutex progressMutex_;
		tbb::mutex globalMutex_;
		std::ofstream debugOut_;
		std::vector<std::vector<Edge> > syntenyPath_;
		std::vector<std::vector<Assignment> > blockId_;
#ifdef _DEBUG_OUT_
		bool debug_;
		std::set<int64_t> missingVertex_;
#endif
	};
}

#endif
