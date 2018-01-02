#ifndef _JUNCTION_STORAGE_H_
#define _JUNCTION_STORAGE_H_

#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <algorithm>

#include <tbb/mutex.h>

#include "junctionapi.h"
#include "streamfastaparser.h"


namespace Sibelia
{	
	using std::min;
	using std::max;

	class Edge
	{
	public:
		Edge() : startVertex_(INT64_MAX), endVertex_(INT64_MAX) {}

		Edge(int64_t startVertex, int64_t endVertex, char ch, char revCh, int32_t length, int32_t capacity) :
			startVertex_(startVertex), endVertex_(endVertex), ch_(ch), revCh_(revCh), length_(length), capacity_(capacity)
		{

		}

		int64_t GetStartVertex() const
		{
			return startVertex_;
		}

		int64_t GetEndVertex() const
		{
			return endVertex_;
		}

		char GetChar() const
		{
			return ch_;
		}

		int64_t GetLength() const
		{
			return length_;
		}

		int64_t GetCapacity() const
		{
			return capacity_;
		}

		Edge Reverse() const
		{
			return Edge(-endVertex_, -startVertex_, revCh_, ch_, length_, capacity_);
		}

		char GetRevChar() const
		{
			return revCh_;
		}

		bool operator < (const Edge & e) const
		{
			if (startVertex_ != e.startVertex_)
			{
				return startVertex_ < e.startVertex_;
			}

			if (endVertex_ != e.endVertex_)
			{
				return endVertex_ < e.endVertex_;
			}

			if (ch_ != e.ch_)
			{
				return ch_ < e.ch_;
			}

			return false;
		}

		bool Valid() const
		{
			return startVertex_ != INT64_MAX;
		}

		bool operator == (const Edge & e) const
		{
			return startVertex_ == e.startVertex_ && endVertex_ == e.endVertex_ && ch_ == e.ch_;
		}

		bool operator != (const Edge & e) const
		{
			return !(*this == e);
		}
		
		void Inc()
		{
			capacity_++;
		}

	private:
		int64_t startVertex_;
		int64_t endVertex_;
		char ch_;
		char revCh_;		
		int32_t length_;
		int32_t capacity_;
	};

	class JunctionStorage
	{
	private:
		
		struct Vertex
		{
			int32_t id;
			int32_t chr;
			int32_t idx;
			int32_t pos;			
			char ch;
			char revCh;

			Vertex(const TwoPaCo::JunctionPosition & junction) : id(junction.GetId()), chr(junction.GetChr()), pos(junction.GetPos())
			{

			}

			Vertex(int32_t chr, int32_t idx) : chr(chr), idx(idx)
			{

			}

			static bool CompareForward(const Vertex & v1, const Vertex & v2)
			{
				if (v1.chr != v2.chr)
				{
					return v1.chr < v2.chr;
				}

				return v1.idx < v2.idx;
			}

			static bool CompareBackward(const Vertex & v1, const Vertex & v2)
			{
				if (v1.chr != v2.chr)
				{
					return v1.chr > v2.chr;
				}

				return v1.idx > v2.idx;
			}
		}; 
		
		struct Position
		{
			int32_t id;
			int32_t pos;
			std::atomic<bool> used;

			Position() : used(false) 
			{

			}

			void Assign(const TwoPaCo::JunctionPosition & junction)
			{
				id = junction.GetId();
				pos = junction.GetPos();
			}
		};

		typedef std::vector<Vertex> VertexVector;
		typedef std::vector<Position> PositionVector;

	public:		

		class JunctionSequentialIterator
		{
		public:
			JunctionSequentialIterator() : idx_(0)
			{

			}

			bool IsPositiveStrand() const
			{
				return chrId_ > 0;
			}

			int64_t GetVertexId() const
			{
				return IsPositiveStrand() ? JunctionStorage::this_->position_[GetChrId()][idx_].id : -JunctionStorage::this_->position_[GetChrId()][idx_].id;
			}

			int64_t GetPosition() const
			{
				if (IsPositiveStrand())
				{
					return JunctionStorage::this_->position_[GetChrId()][idx_].pos;
				}

				return JunctionStorage::this_->position_[GetChrId()][idx_].pos + JunctionStorage::this_->k_;
			}

			int64_t GetAbsolutePosition() const
			{
				return JunctionStorage::this_->position_[GetChrId()][idx_].pos;
			}

			Edge OutgoingEdge() const
			{
				const Position & now = JunctionStorage::this_->position_[GetChrId()][idx_];
				if (IsPositiveStrand())
				{
					const Position & next = JunctionStorage::this_->position_[GetChrId()][idx_ + 1];
					char ch = JunctionStorage::this_->sequence_[GetChrId()][now.pos + JunctionStorage::this_->k_];
					char revCh = TwoPaCo::DnaChar::ReverseChar(JunctionStorage::this_->sequence_[GetChrId()][next.pos - 1]);
					return Edge(now.id, next.id, ch, revCh, next.pos - now.pos, 1);
				}
				else
				{
					const Position & next = JunctionStorage::this_->position_[GetChrId()][idx_ - 1];
					char ch = TwoPaCo::DnaChar::ReverseChar(JunctionStorage::this_->sequence_[GetChrId()][now.pos - 1]);
					char revCh = JunctionStorage::this_->sequence_[GetChrId()][now.pos + JunctionStorage::this_->k_];
					return Edge(-now.id, -next.id, ch, revCh, now.pos - next.pos, 1);
				}
			}

			Edge IngoingEdge() const
			{
				const Position & now = JunctionStorage::this_->position_[GetChrId()][idx_];
				if (IsPositiveStrand())
				{					
					const Position & prev = JunctionStorage::this_->position_[GetChrId()][idx_ - 1];
					char ch = JunctionStorage::this_->sequence_[GetChrId()][prev.pos + JunctionStorage::this_->k_];
					char revCh = TwoPaCo::DnaChar::ReverseChar(JunctionStorage::this_->sequence_[GetChrId()][now.pos - 1]);
					return Edge(prev.id, now.id, ch, revCh, now.pos - prev.pos, 1);				
				}
				else
				{
					const Position & prev = JunctionStorage::this_->position_[GetChrId()][idx_ + 1];
					char ch = TwoPaCo::DnaChar::ReverseChar(JunctionStorage::this_->sequence_[GetChrId()][prev.pos - 1]);
					char revCh = JunctionStorage::this_->sequence_[GetChrId()][now.pos + JunctionStorage::this_->k_];
					return Edge(-prev.id, -now.id, ch, revCh, prev.pos - now.pos, 1);					
				}
			}

			JunctionSequentialIterator Reverse()
			{
				return JunctionSequentialIterator(GetChrId(), idx_, !IsPositiveStrand());
			}

			char GetChar() const
			{
				int64_t pos = JunctionStorage::this_->position_[GetChrId()][idx_].pos;
				if (IsPositiveStrand())
				{
					return JunctionStorage::this_->sequence_[GetChrId()][pos + JunctionStorage::this_->k_];
				}

				return TwoPaCo::DnaChar::ReverseChar(JunctionStorage::this_->sequence_[GetChrId()][pos - 1]);
			}

			uint64_t GetIndex() const
			{
				return idx_;
			}

			uint64_t GetRelativeIndex() const
			{
				if (IsPositiveStrand())
				{
					return idx_;
				}

				return JunctionStorage::this_->chrSize_[GetChrId()] - idx_ - 1;
			}

			uint64_t GetChrId() const
			{
				return abs(chrId_) - 1;
			}

			bool Valid() const
			{
				return idx_ >= 0 && idx_ < JunctionStorage::this_->chrSize_[GetChrId()];
			}

			bool IsUsed() const
			{
				bool ret = JunctionStorage::this_->position_[GetChrId()][idx_].used;
				return ret;
			}

			void MarkUsed() const
			{
				JunctionStorage::this_->position_[GetChrId()][idx_].used = true;
			}

			JunctionSequentialIterator& operator++ ()
			{
				Inc();
				return *this;
			}

			JunctionSequentialIterator operator++ (int)
			{
				JunctionSequentialIterator ret(*this);
				Inc();
				return ret;
			}

			JunctionSequentialIterator operator + (size_t step) const
			{
				JunctionSequentialIterator ret(*this);
				ret.Inc(step);
				return ret;
			}

			JunctionSequentialIterator operator - (size_t step) const
			{
				JunctionSequentialIterator ret(*this);
				ret.Dec(step);
				return ret;
			}

			JunctionSequentialIterator& operator-- ()
			{
				Dec();
				return *this;
			}

			JunctionSequentialIterator operator-- (int)
			{
				JunctionSequentialIterator ret(*this);
				Dec();
				return ret;
			}

			JunctionSequentialIterator Next() const
			{
				JunctionSequentialIterator ret(*this);
				return ++ret;
			}

			JunctionSequentialIterator Prev() const
			{
				JunctionSequentialIterator ret(*this);
				return --ret;
			}

			bool operator < (const JunctionSequentialIterator & arg) const
			{
				if (GetChrId() != arg.GetChrId())
				{
					return GetChrId() < arg.GetChrId();
				}

				return GetIndex() < arg.GetIndex();
			}

			bool operator == (const JunctionSequentialIterator & arg) const
			{
				return this->chrId_ == arg.chrId_ && this->idx_ == arg.idx_;
			}

			bool operator != (const JunctionSequentialIterator & arg) const
			{
				return !(*this == arg);
			}

		private:

			void Inc(int64_t step = 1)
			{

				idx_ += IsPositiveStrand() ? +step : -step;
			}

			void Dec(int64_t step = 1)
			{

				idx_ += IsPositiveStrand() ? -step : +step;
			}

			JunctionSequentialIterator(int64_t chrId, int64_t idx, bool isPositiveStrand) : idx_(idx), chrId_(isPositiveStrand ? chrId + 1 : -(chrId + 1))
			{

			}

			friend class JunctionStorage;
			int64_t chrId_;
			int64_t idx_;
		};



		class JunctionIterator
		{
		public:
			JunctionIterator() : vid_(0)
			{

			}

			bool IsPositiveStrand() const
			{
				return JunctionStorage::this_->vertex_[abs(vid_)][iidx_].id == vid_;
			}

			int64_t GetVertexId() const
			{
				return vid_;
			}

			int64_t GetPosition() const
			{
				return JunctionStorage::this_->vertex_[abs(vid_)][iidx_].pos;
			}

			char GetChar() const
			{
				if (IsPositiveStrand())
				{
					return JunctionStorage::this_->vertex_[abs(vid_)][iidx_].ch;
				}

				return JunctionStorage::this_->vertex_[abs(vid_)][iidx_].revCh;
			}

			JunctionSequentialIterator SequentialIterator() const
			{
				return JunctionSequentialIterator(GetChrId(), GetIndex(), IsPositiveStrand());
			}

			uint64_t GetIndex() const
			{
				return JunctionStorage::this_->vertex_[abs(vid_)][iidx_].idx;
			}

			uint64_t GetItIndex() const
			{
				return iidx_;
			}

			uint64_t GetRelativeIndex() const
			{
				if (IsPositiveStrand())
				{
					return JunctionStorage::this_->vertex_[abs(vid_)][iidx_].idx;;
				}

				return JunctionStorage::this_->chrSize_[GetChrId()] - JunctionStorage::this_->vertex_[abs(vid_)][iidx_].idx; - 1;
			}

			uint64_t GetChrId() const
			{
				return JunctionStorage::this_->vertex_[abs(vid_)][iidx_].chr;
			}

			bool Valid() const
			{
				return iidx_ < JunctionStorage::this_->vertex_[abs(vid_)].size();
			}

			size_t InstancesCount() const
			{
				return JunctionStorage::this_->vertex_[abs(vid_)].size();
			}

			bool IsUsed() const
			{
				return JunctionStorage::this_->position_[GetChrId()][GetIndex()].used;
			}

			void MarkUsed() const
			{
				JunctionStorage::this_->position_[GetChrId()][GetIndex()].used = true;
			}

			JunctionIterator operator + (size_t inc) const
			{
				return JunctionIterator(vid_, iidx_ + inc);
			}

			JunctionIterator& operator++ ()
			{
				++iidx_;
				return *this;
			}
		
			JunctionIterator operator++ (int)
			{
				JunctionIterator ret(*this);
				++iidx_;
				return ret;
			}

			JunctionIterator& operator-- ()
			{
				--iidx_;
				return *this;
			}

			JunctionIterator operator-- (int)
			{
				JunctionIterator ret(*this);
				--iidx_;
				return ret;
			}

			bool operator < (const JunctionIterator & arg) const
			{
				if (GetChrId() != arg.GetChrId())
				{
					return GetChrId() < arg.GetChrId();
				}

				return GetIndex() < arg.GetIndex();
			}

			bool operator == (const JunctionIterator & arg) const
			{
				return this->vid_ == arg.vid_ && this->iidx_ == arg.iidx_;
			}

			bool operator != (const JunctionIterator & arg) const
			{
				return !(*this == arg);
			}

			JunctionIterator(int64_t vid) : iidx_(0), vid_(vid)
			{
			}

		private:			
			
			JunctionIterator(int64_t vid, int64_t iidx) : iidx_(iidx), vid_(vid)
			{
			}

			friend class JunctionStorage;
			int64_t iidx_;
			int64_t vid_;

		};

		void LockRange(JunctionSequentialIterator start, JunctionSequentialIterator end, std::pair<size_t, size_t> & prevIdx)
		{
			do
			{
				size_t idx = MutexIdx(start.GetChrId(), start.GetIndex());
				if (start.GetChrId() != prevIdx.first || idx != prevIdx.second)
				{
					mutex_[start.GetChrId()][idx].lock();
					prevIdx.first = start.GetChrId();
					prevIdx.second = idx;
				}
				
				
			} while (start++ != end);
		}

		void UnlockRange(JunctionSequentialIterator start, JunctionSequentialIterator end, std::pair<size_t, size_t> & prevIdx)
		{
			do
			{
				size_t idx = MutexIdx(start.GetChrId(), start.GetIndex());
				if (start.GetChrId() != prevIdx.first || idx != prevIdx.second)
				{
					mutex_[start.GetChrId()][idx].unlock();
					prevIdx.first = start.GetChrId();
					prevIdx.second = idx;
				}
				

			} while (start++ != end);
		}

		int64_t GetChrNumber() const
		{
			return position_.size();
		}

		const std::string& GetChrSequence(uint64_t idx) const
		{
			return sequence_[idx];
		}

		const std::string& GetChrDescription(uint64_t idx) const
		{
			return sequenceDescription_[idx];
		}

		int64_t GetChrVerticesCount(uint64_t chrId) const
		{
			return chrSize_[chrId];
		}
		
		JunctionSequentialIterator GetIterator(uint64_t chrId, uint64_t idx, bool isPositiveStrand = true) const
		{
			return JunctionSequentialIterator(chrId, idx, isPositiveStrand);
		}

		JunctionSequentialIterator Begin(uint64_t chrId, bool isPositiveStrand = true) const
		{
			return JunctionSequentialIterator(chrId, 0, isPositiveStrand);
		}

		JunctionSequentialIterator End(uint64_t chrId, bool isPositiveStrand = true) const
		{
			return JunctionSequentialIterator(chrId, chrSize_[chrId], isPositiveStrand);
		}

		JunctionIterator InstanceExtensionForward(JunctionSequentialIterator back, int64_t vid) const
		{
			int64_t adjVid = abs(vid);
			if (back.IsPositiveStrand())
			{
				auto it = std::upper_bound(vertex_[adjVid].begin(), vertex_[adjVid].end(), Vertex(back.GetChrId(), back.GetIndex()), Vertex::CompareForward);
				if (it != vertex_[adjVid].end())
				{
					return JunctionIterator(vid, it - vertex_[adjVid].begin());
				}
			}
			else
			{
				auto it = std::upper_bound(vertex_[adjVid].rbegin(), vertex_[adjVid].rend(), Vertex(back.GetChrId(), back.GetIndex()), Vertex::CompareBackward);
				if (it != vertex_[adjVid].rend())
				{
					size_t ridx = it - vertex_[adjVid].rbegin();
					return JunctionIterator(vid, vertex_[adjVid].size() - ridx - 1);
				}
			}
			
			return JunctionIterator();
		}

		JunctionIterator InstanceExtensionBackward(JunctionSequentialIterator back, int64_t vid) const
		{
			int64_t adjVid = abs(vid);
			if (back.IsPositiveStrand())
			{
				auto it = std::upper_bound(vertex_[adjVid].rbegin(), vertex_[adjVid].rend(), Vertex(back.GetChrId(), back.GetIndex()), Vertex::CompareBackward);
				if (it != vertex_[adjVid].rend())
				{
					size_t ridx = it - vertex_[adjVid].rbegin();
					return JunctionIterator(vid, vertex_[adjVid].size() - ridx - 1);
				}
			}
			else
			{
				auto it = std::upper_bound(vertex_[adjVid].begin(), vertex_[adjVid].end(), Vertex(back.GetChrId(), back.GetIndex()), Vertex::CompareForward);
				if (it != vertex_[adjVid].end())
				{
					return JunctionIterator(vid, it - vertex_[adjVid].begin());
				}
			}

			return JunctionIterator();
		}

		int64_t GetVerticesNumber() const
		{
			return vertex_.size();
		}
				
		uint64_t GetInstancesCount(int64_t vertexId) const
		{
			return vertex_[abs(vertexId)].size();
		}		

		size_t MutexNumber() const
		{
			return 1 << mutexBits_;
		}

		void Init(const std::string & inFileName, const std::string & genomesFileName, int64_t threads)
		{
			this_ = this;
			{
				TwoPaCo::JunctionPositionReader reader(inFileName);
				for (TwoPaCo::JunctionPosition junction; reader.NextJunctionPosition(junction);)
				{
					if (junction.GetChr() >= chrSize_.size())
					{
						chrSize_.push_back(0);
					}

					++chrSize_.back();
				}
			}

			position_.resize(chrSize_.size());
			for(size_t i = 0; i < chrSize_.size(); i++)
			{
				position_[i].reset(new Position[chrSize_[i]]);
			}

			{
				size_t idx = 0;
				size_t chr = 0;
				TwoPaCo::JunctionPositionReader reader(inFileName);
				for (TwoPaCo::JunctionPosition junction; reader.NextJunctionPosition(junction);)
				{
					if (junction.GetChr() > chr)
					{
						chr++;
						idx = 0;
					}

					position_[junction.GetChr()][idx].Assign(junction);				
					size_t absId = abs(junction.GetId());
					while (absId >= vertex_.size())
					{
						vertex_.push_back(VertexVector());
					}

					vertex_[absId].push_back(Vertex(junction));
					vertex_[absId].back().idx = idx++;
				}
			}

			size_t record = 0;
			sequence_.resize(position_.size());
			for (TwoPaCo::StreamFastaParser parser(genomesFileName); parser.ReadRecord(); record++)
			{
				sequenceDescription_.push_back(parser.GetCurrentHeader());
				for (char ch; parser.GetChar(ch); )
				{
					sequence_[record].push_back(ch);
				}				
			}

			for (size_t i = 0; i < vertex_.size(); i++)
			{
				for (size_t j = 0; j < vertex_[i].size(); j++)
				{
					int64_t chr = vertex_[i][j].chr;
					int64_t pos_ = vertex_[i][j].pos;
					vertex_[i][j].ch = sequence_[chr][pos_ + JunctionStorage::this_->k_];
					vertex_[i][j].revCh = pos_ > 0 ? TwoPaCo::DnaChar::ReverseChar(sequence_[chr][pos_ - 1]) : 'N';
				}

				std::sort(vertex_[i].begin(), vertex_[i].end(), Vertex::CompareForward);
			}
			
			mutex_.resize(GetChrNumber());
			chrSizeBits_.resize(GetChrNumber(), 1);
			for (mutexBits_ = 3; (1 << mutexBits_) < threads * (1 << 7); mutexBits_++);
			for (size_t i = 0; i < mutex_.size(); i++) 
			{
				mutex_[i].reset(new tbb::mutex[1 << mutexBits_]);
				for (; (int64_t(1) << chrSizeBits_[i]) <= chrSize_[i]; chrSizeBits_[i]++);
				chrSizeBits_[i] = max(int64_t(0), chrSizeBits_[i] - mutexBits_);
			}
		}					

		JunctionStorage() {}
		JunctionStorage(const std::string & fileName, const std::string & genomesFileName, uint64_t k, int64_t threads) : k_(k)
		{
			Init(fileName, genomesFileName, threads);
		}


	private:
		
		struct LightEdge
		{
			int64_t vertex;
			char ch;
		};

		size_t MutexIdx(size_t chrId, size_t idx) const
		{
			size_t ret = idx >> chrSizeBits_[chrId];
			assert(ret < MutexNumber());
			return ret;
		}

		int64_t k_;
		int64_t mutexBits_;
		std::vector<std::string> sequence_;
		std::vector<std::string> sequenceDescription_;		
		std::vector<int64_t> chrSizeBits_;
		std::vector<size_t> chrSize_;
		std::vector<VertexVector> vertex_;
		std::vector<std::unique_ptr<Position[]> > position_;
		std::vector<std::unique_ptr<tbb::mutex[]> > mutex_;
		static JunctionStorage * this_;
	};
}

#endif
