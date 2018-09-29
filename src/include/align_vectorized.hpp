/**
 * @file    align_vectorized.hpp
 * @brief   vectorized routines to perform alignment
 * @author  Chirag Jain <cjain7@gatech.edu>
 */


#ifndef GRAPH_ALIGN_VEC_HPP
#define GRAPH_ALIGN_VEC_HPP

#include <immintrin.h>

#include "graphLoad.hpp"
#include "csr_char.hpp"
#include "graph_iter.hpp"
#include "base_types.hpp"
#include "utils.hpp"

//External includes
#include "kseq.h"
#include "prettyprint.hpp"
#include "aligned_allocator.hpp"

#define DUMMY                     'B'
#define SIMD_REG_SIZE             512 

namespace psgl
{
  /**
   * Parameters and SIMD instructions specific for certain score type.
   */

  template<typename T> struct SimdInst {};

  template<>
    struct SimdInst<int32_t> 
    {
      typedef int32_t type;
      static constexpr int numSeqs = SIMD_REG_SIZE / (8 * sizeof(type));

      static inline __m512i add(const __m512i& a, const __m512i& b) { return _mm512_add_epi32(a, b); }
      static inline __m512i set1(int32_t a) { return _mm512_set1_epi32(a); }
      static inline __m512i set1_32(int32_t a) { return _mm512_set1_epi32(a); }
      static inline __m512i mask_set1(const __m512i& a, __mmask16 k, int32_t b) { return _mm512_mask_set1_epi32(a, k, b); } 
      static inline __m512i mask_set1_32(const __m512i& a, __mmask16 k, int32_t b) { return _mm512_mask_set1_epi32(a, k, b); } 
      static inline __m512i blend(__mmask16 k, const __m512i& a, const __m512i& b) {return _mm512_mask_blend_epi32(k, a, b); }
      static inline __m512i max(const __m512i& a, const __m512i& b) { return _mm512_max_epi32(a, b); }
      static inline __mmask16 cmpeq(const __m512i& a, const __m512i& b) { return _mm512_cmpeq_epi32_mask(a, b); }  
      static inline __mmask16 cmpeq_32(const __m512i& a, const __m512i& b) { return _mm512_cmpeq_epi32_mask(a, b); }  

      //type oblivious operations
      static inline __m512i zero() {return _mm512_setzero_si512(); }
      static inline void store(void *mem_addr, const __m512i &a) { return _mm512_store_si512(mem_addr, a); }
      static inline __m512i load(void const *mem_addr) { return _mm512_load_si512(mem_addr); }
    };

  template<>
    struct SimdInst<int16_t> 
    {
      typedef int16_t type;
      static constexpr int numSeqs = SIMD_REG_SIZE / (8 * sizeof(type));

      static inline __m512i add(const __m512i& a, const __m512i& b) { return _mm512_add_epi16(a, b); }
      static inline __m512i set1(int16_t a) { return _mm512_set1_epi16(a); }
      static inline __m512i set1_32(int32_t a) { return _mm512_set1_epi32(a); }
      static inline __m512i mask_set1(const __m512i& a, __mmask32 k, int16_t b) { return _mm512_mask_set1_epi16(a, k, b); } 
      static inline __m512i mask_set1_32(const __m512i& a, __mmask16 k, int32_t b) { return _mm512_mask_set1_epi32(a, k, b); } 
      static inline __m512i blend(__mmask32 k, const __m512i& a, const __m512i& b) {return _mm512_mask_blend_epi16(k, a, b); }
      static inline __m512i max(const __m512i& a, const __m512i& b) { return _mm512_max_epi16(a, b); }
      static inline __mmask32 cmpeq(const __m512i& a, const __m512i& b) { return _mm512_cmpeq_epi16_mask(a, b); }  
      static inline __mmask16 cmpeq_32(const __m512i& a, const __m512i& b) { return _mm512_cmpeq_epi32_mask(a, b); }  

      //type oblivious operations
      static inline __m512i zero() {return _mm512_setzero_si512(); }
      static inline void store(void *mem_addr, const __m512i &a) { return _mm512_store_si512(mem_addr, a); }
      static inline __m512i load(void const *mem_addr) { return _mm512_load_si512(mem_addr); }
    };

  template<>
    struct SimdInst<int8_t> 
    {
      typedef int8_t type;
      static constexpr int numSeqs = SIMD_REG_SIZE / (8 * sizeof(type));

      static inline __m512i add(const __m512i& a, const __m512i& b) { return _mm512_add_epi8(a, b); }
      static inline __m512i set1(int8_t a) { return _mm512_set1_epi8(a); }
      static inline __m512i set1_32(int32_t a) { return _mm512_set1_epi32(a); }
      static inline __m512i mask_set1(const __m512i& a, __mmask64 k, int8_t b) { return _mm512_mask_set1_epi8(a, k, b); } 
      static inline __m512i mask_set1_32(const __m512i& a, __mmask16 k, int32_t b) { return _mm512_mask_set1_epi32(a, k, b); } 
      static inline __m512i blend(__mmask64 k, const __m512i& a, const __m512i& b) {return _mm512_mask_blend_epi8(k, a, b); }
      static inline __m512i max(const __m512i& a, const __m512i& b) { return _mm512_max_epi8(a, b); }
      static inline __mmask64 cmpeq(const __m512i& a, const __m512i& b) { return _mm512_cmpeq_epi8_mask(a, b); }  
      static inline __mmask16 cmpeq_32(const __m512i& a, const __m512i& b) { return _mm512_cmpeq_epi32_mask(a, b); }  

      //type oblivious operations
      static inline __m512i zero() {return _mm512_setzero_si512(); }
      static inline void store(void *mem_addr, const __m512i &a) { return _mm512_store_si512(mem_addr, a); }
      static inline __m512i load(void const *mem_addr) { return _mm512_load_si512(mem_addr); }
    };

  /**
   * @brief   Supports phase 1 DP in forward direction
   */
  template <typename SIMD>
    class Phase1_Vectorized
    {
      private:

        //reference graph
        const CSR_char_container &graph;

        // pre-compute which graph vertices are connected with hop longer
        // than 'blockWidth'
        std::vector<bool> withLongHop;

        //for converting input reads into SOA to enable vectorization
        std::vector<char> readSetSOA;

        //cumulative read batch sizes
        std::vector<size_t> readSetSOAPrefixSum;

        //input reads
        const std::vector<std::string> &readSet;

        //read lengths in their 'sorted order'
        std::vector<size_t> sortedReadLengths;

        //sorted permutation order of input reads
        std::vector<size_t> sortedReadOrder;

      public:

        //small temporary storage buffer for DP scores
        //should be a power of 2
        static constexpr size_t blockWidth = 8; 

        //process these many vertical cells in a go
        //should be a power of 2
        static constexpr size_t blockHeight = 16;   

        /**
         * @brief                   public constructor
         * @param[in]   readSet     vector of input query sequences to align
         * @param[in]   g           input reference graph
         */
        Phase1_Vectorized(const std::vector<std::string> &readSet, 
            const CSR_char_container &g) :
          readSet (readSet), graph (g)
        {
          this->sortReadsForLoadBalance();
          this->convertToSOA();
          this->computeLongHops();
        };

        /**
         * @brief                                 wrapper function for phase 1 routine 
         * @param[out]  outputBestScoreVector     vector to keep value and location of best scores,
         *                                        vector size is same as count of the reads
         * @note                                  this class won't care about rev. complement of sequences
         */
        template <typename Vec>
          void alignToDAGLocal_Phase1_vectorized_wrapper(Vec &outputBestScoreVector) const
          {
            assert (outputBestScoreVector.size() == readSet.size());

            std::size_t countReadBatches = std::ceil (readSet.size() * 1.0 / SIMD::numSeqs);

            //column location value requires int32_t type, so may need >1 register per read batch
            constexpr size_t colValuesPerRegister = SIMD_REG_SIZE / (8 * sizeof(int32_t));
            constexpr size_t colRegistersCountPerBatch = SIMD::numSeqs / colValuesPerRegister;

            static_assert ( colRegistersCountPerBatch == 1 || 
                            colRegistersCountPerBatch == 2 || 
                            colRegistersCountPerBatch == 4); 

            // modified containers to hold best-score info
            // vector elements aligned to 64 byte boundaries
            std::vector <__m512i, aligned_allocator<__m512i, 64> > _bestScoreVector (countReadBatches);
            std::vector <__m512i, aligned_allocator<__m512i, 64> > 
                  _bestScoreColVector (countReadBatches * colRegistersCountPerBatch);
            std::vector <__m512i, aligned_allocator<__m512i, 64> > _bestScoreRowVector (countReadBatches);

            // execute the alignment routine
            this->alignToDAGLocal_Phase1_vectorized (_bestScoreVector, _bestScoreColVector, _bestScoreRowVector); 

            //when debugging for low-precision
#ifdef DEBUG
            for (auto &e : _bestScoreVector)
              simdUtils::print_avx_num16 (e);

            for (auto &e : _bestScoreColVector)
              simdUtils::print_avx_num32 (e);

            for (auto &e : _bestScoreRowVector)
              simdUtils::print_avx_num16 (e);
#endif


            //parse best scores from vector registers
            std::vector<typename SIMD::type, aligned_allocator<typename SIMD::type, 64> > storeScores (SIMD::numSeqs);
            std::vector<typename SIMD::type, aligned_allocator<typename SIMD::type, 64> > storeRows   (SIMD::numSeqs);
            std::vector<int32_t,             aligned_allocator<int32_t,             64> > storeCols   (SIMD::numSeqs);

            for (size_t i = 0; i < countReadBatches; i++)
            {
              SIMD::store (storeScores.data(), _bestScoreVector[i]);
              SIMD::store (storeRows.data(), _bestScoreRowVector[i]);

              for (size_t j = 0; j < colRegistersCountPerBatch; j++) 
              {
                SIMD::store ( &storeCols [j*colValuesPerRegister], 
                              _bestScoreColVector[i * colRegistersCountPerBatch + j]);
              }

              for (size_t j = 0; j < SIMD::numSeqs; j++)
              {
                if (i * SIMD::numSeqs + j < readSet.size())
                {
                  auto originalReadId = sortedReadOrder[i * SIMD::numSeqs + j];

                  outputBestScoreVector[originalReadId].score         = storeScores[j];
                  outputBestScoreVector[originalReadId].refColumnEnd  = storeCols[j];
                  outputBestScoreVector[originalReadId].qryRowEnd     = storeRows[j];

#ifdef DEBUG
                  std::cout << "INFO, psgl::Phase1_Vectorized::alignToDAGLocal_Phase1_vectorized_wrapper, read # " << originalReadId << ",  score = " << storeScores[j] << ", qryRowEnd = " << storeRows[j] << ", refColumnEnd = " << storeCols[j] << "\n";
#endif
                }
              }
            }
          }

      private:

        /**
         * @brief       compute the sorted order of sequences for load balancing
         * @details     sorting is done in decreasing length order
         */
        void sortReadsForLoadBalance()
        {
          typedef std::pair<size_t, size_t> pair_t;

          //vector of tuples of length and index of reads
          std::vector<pair_t> lengthTuples;
           
          for(size_t i = 0; i < this->readSet.size(); i++)
            lengthTuples.emplace_back (readSet[i].length(), i);

          //sort in descending order, longer reads first
          std::sort (lengthTuples.begin(), lengthTuples.end(), std::greater<pair_t>() );

          for(auto &e : lengthTuples)
          {
            this->sortedReadLengths.push_back (e.first);
            this->sortedReadOrder.push_back (e.second);
          }
        }

        /**
         * @brief       convert input reads characters into SOA to enable vectorization
         */
        void convertToSOA()
        {
          assert (readSet.size() > 0);
          assert (sortedReadOrder.size() == readSet.size());
          assert (readSetSOA.size() == 0);

          /**
           * Requirements from the padding process
           * - each read length be a multiple of 'blockHeight'
           * - count of reads be a multiple of SIMD::numSeqs
           * - each batch of SIMD::numSeqs reads should be of equal length
           */

          //re-arrange read characters for vectorized processing; 
          //SIMD::numSeqs reads at a time, sorted in decreasing order by length
          
          auto readCount = readSet.size();

          readSetSOAPrefixSum.push_back(0);

          for (size_t i = 0; i < readCount; i += SIMD::numSeqs)
          {
            auto batchLength = readSet[sortedReadOrder[i]].length();  //longest read in this batch
            batchLength += blockHeight - 1 - (batchLength - 1) % blockHeight; //round-up

            for (size_t j = 0; j < batchLength; j++)
              for (size_t k = 0; k < SIMD::numSeqs; k++)
                if ( i + k < readCount && j < readSet[sortedReadOrder[i+k]].length() )
                  readSetSOA.push_back ( readSet[sortedReadOrder[i + k]][j] );
                else
                  readSetSOA.push_back (DUMMY);

            readSetSOAPrefixSum.push_back (readSetSOA.size());
          }

          std::size_t countReadBatches = std::ceil (readSet.size() * 1.0 / SIMD::numSeqs);
          assert (readSetSOAPrefixSum.size() == countReadBatches + 1);
        }

        /**
         * @brief     precompute which vertices are connected with long edge hops
         *            i.e. longer than 'blockWidth'
         */
        void computeLongHops()
        {
          assert (withLongHop.size() == 0);

          this->withLongHop.resize(graph.numVertices, false);

          for(int32_t i = 0; i < graph.numVertices; i++)
          {
            for(auto j = graph.offsets_in[i]; j < graph.offsets_in[i+1]; j++)
            {
              auto from_pos = graph.adjcny_in[j];
              auto to_pos = i;

              //compare hop distance to 'blockWidth'
              if (to_pos - from_pos >= this->blockWidth)
                this->withLongHop[from_pos] = true;
            }
          }

#ifdef DEBUG
          auto trueCount = std::count(withLongHop.begin(), withLongHop.end(), true);
          std::cout << "INFO, psgl::Phase1_Vectorized::computeLongHops, fraction of hops that are long: " << trueCount * 1.0 / graph.numVertices << "\n";
#endif
        }

        /**
         * @brief                         execute first phase of alignment i.e. compute DP and 
         *                                find locations of the best alignment of each read
         * @param[out]  bestScores        best DP scores of reads
         * @param[out]  bestCols          columns where best alignment ends (for traceback later)
         * @param[out]  bestRows          rows where best alignment ends
         */
        template <typename Vec>
          void alignToDAGLocal_Phase1_vectorized (Vec &bestScores, Vec &bestCols, Vec &bestRows) const
          {
            std::size_t readCount = readSet.size();
            std::size_t countReadBatches = std::ceil (readCount * 1.0 / SIMD::numSeqs);

            //column location value requires int32_t type, so may need >1 register per read batch
            constexpr size_t colValuesPerRegister = SIMD_REG_SIZE / (8 * sizeof(int32_t));
            constexpr size_t colRegistersCountPerBatch = SIMD::numSeqs / colValuesPerRegister;

            static_assert ( colRegistersCountPerBatch == 1 || 
                            colRegistersCountPerBatch == 2 || 
                            colRegistersCountPerBatch == 4); 

            //few checks
            assert (bestScores.size() == countReadBatches);
            assert (bestCols.size() == countReadBatches * colRegistersCountPerBatch);
            assert (bestRows.size() == countReadBatches);

#ifdef VTUNE_SUPPORT
            __itt_resume();
#endif

            //init best score vector to zero bits
            std::fill (bestScores.begin(), bestScores.end(), SIMD::zero() );
            std::fill (bestCols.begin(), bestCols.end(), SIMD::zero() );
            std::fill (bestRows.begin(), bestRows.end(), SIMD::zero() );

            //init score simd vectors
            __m512i match512    = SIMD::set1 ((typename SIMD::type) SCORE::match);
            __m512i mismatch512 = SIMD::set1 ((typename SIMD::type) SCORE::mismatch);
            __m512i del512      = SIMD::set1 ((typename SIMD::type) SCORE::del);
            __m512i ins512      = SIMD::set1 ((typename SIMD::type) SCORE::ins);

            std::vector<double> threadTimings (omp_get_max_threads(), 0);

#pragma omp parallel
            {
#pragma omp barrier
              threadTimings[omp_get_thread_num()] = omp_get_wtime();

              //create local copy of graph for faster access
              const CSR_char_container graphLocal = this->graph;
              const std::vector<bool> withLongHopLocal = withLongHop;

              //type def. for memory-aligned vector allocation for SIMD instructions
              using AlignedVecType = std::vector <__m512i, aligned_allocator<__m512i, 64> >;

              //buffer to save selected columns (associated with long hops) of DP matrix
              std::size_t countLongHops = std::count (withLongHopLocal.begin(), withLongHopLocal.end(), true);
              AlignedVecType fartherColumnsBuffer (countLongHops * this->blockHeight);

              //for convenient access to 2D buffer
              std::vector<__m512i*> fartherColumns(graphLocal.numVertices);
              {
                size_t j = 0;

                for(int32_t i = 0; i < graphLocal.numVertices; i++)
                  if ( withLongHopLocal[i] )
                    fartherColumns[i] = &fartherColumnsBuffer[ (j++) * this->blockHeight ];
              }

              //buffer to save neighboring column scores
              AlignedVecType nearbyColumnsBuffer (this->blockWidth * this->blockHeight);

              //for convenient access to 2D buffer
              std::vector<__m512i*> nearbyColumns (this->blockWidth);
              {
                for (std::size_t i = 0; i < this->blockWidth; i++)
                  nearbyColumns[i] = &nearbyColumnsBuffer[i * this->blockHeight];
              }

              //buffer to save scores of last row in each iteration
              //one row for writing and one for reading
              AlignedVecType lastBatchRowBuffer (2 * graphLocal.numVertices);

              //for convenient access to 2D buffer
              std::vector<__m512i*> lastBatchRow (2);
              {
                lastBatchRow[0] = &lastBatchRowBuffer[0];
                lastBatchRow[1] = &lastBatchRowBuffer[graphLocal.numVertices];
              }

              //buffer to save read charactes for innermost loop
              std::vector<typename SIMD::type, aligned_allocator<typename SIMD::type, 64> > readCharsInt (SIMD::numSeqs * this->blockHeight);

              //process SIMD::numSeqs reads in a single iteration
#pragma omp for schedule(dynamic) nowait
              for (size_t i = 0; i < countReadBatches; i++)
              {
                __m512i bestScores512 = SIMD::zero();
                __m512i bestRows512   = SIMD::zero();

                //we may need at most 4 registers to save column for each batch (depending on SIMD::type)
                __m512i bestCols512_0   = SIMD::zero();
                __m512i bestCols512_1   = SIMD::zero();
                __m512i bestCols512_2   = SIMD::zero();
                __m512i bestCols512_3   = SIMD::zero();

                //reset DP 'lastBatchRow' buffer
                std::fill (lastBatchRowBuffer.begin(), lastBatchRowBuffer.end(), SIMD::zero());

                int32_t qryBatchLength = readSet[sortedReadOrder[i * SIMD::numSeqs]].length();  //longest read in this batch
                qryBatchLength += this->blockHeight - 1 - (qryBatchLength - 1) % this->blockHeight; //round-up

                //iterate over read length (process more than 1 characters in batch)
                for (int32_t j = 0; j < qryBatchLength; j += this->blockHeight)
                {
                  //loop counter 
                  size_t loopJ = j / (this->blockHeight);

                  //convert read character to int32_t
                  for (int32_t k = 0; k < SIMD::numSeqs * this->blockHeight ; k++)
                  {
                    readCharsInt [k] = readSetSOA [readSetSOAPrefixSum[i] + j*SIMD::numSeqs + k];
                  }

                  //iterate over characters in reference graph
                  for (int32_t k = 0; k < graphLocal.numVertices; k++)
                  {
                    //current reference character
                    __m512i graphChar = SIMD::set1 ((typename SIMD::type) graphLocal.vertex_label[k] );

                    //current best score, init to 0
                    __m512i currentMax512;

                    //iterate over 'blockHeight' read characters
                    for (size_t l = 0; l < this->blockHeight; l++)
                    {
                      //load read characters
                      __m512i readChars = SIMD::load ( &readCharsInt[l * SIMD::numSeqs] );

                      //current best score, init to 0
                      currentMax512 = SIMD::zero();

                      //see if query and reference character match
                      auto compareChar = SIMD::cmpeq (readChars, graphChar);
                      __m512i sub512 = SIMD::blend (compareChar, mismatch512, match512);

                      //match-mismatch edit
                      currentMax512 = SIMD::max (currentMax512, sub512); //local alignment can also start with a match at this char 

                      //iterate over graph neighbors
                      //which buffers to access depends on the value of 'l'
                      if (l == 0)
                      {
                        for(size_t m = graphLocal.offsets_in[k]; m < graphLocal.offsets_in[k+1]; m++)
                        {
                          //paths with match mismatch edit
                          __m512i substEdit = SIMD::add ( lastBatchRow[(loopJ - 1) & 1][ graphLocal.adjcny_in[m] ], sub512);
                          currentMax512 = SIMD::max (currentMax512, substEdit); 

                          //paths with deletion edit
                          __m512i delEdit;

                          if (k - graphLocal.adjcny_in[m] < this->blockWidth)
                            delEdit = SIMD::add ( nearbyColumns[graphLocal.adjcny_in[m] & (blockWidth-1)][l], del512);
                          else
                            delEdit = SIMD::add ( fartherColumns[graphLocal.adjcny_in[m]][l], del512);

                          currentMax512 = SIMD::max (currentMax512, delEdit); 
                        }

                        //insertion edit
                        __m512i insEdit = SIMD::add (lastBatchRow[(loopJ - 1) & 1][k], ins512);
                        currentMax512 = SIMD::max (currentMax512, insEdit);
                      }
                      else
                      {
                        for(size_t m = graphLocal.offsets_in[k]; m < graphLocal.offsets_in[k+1]; m++)
                        {
                          //paths with match mismatch edit
                          __m512i substEdit;

                          //paths with deletion edit
                          __m512i delEdit;

                          if (k - graphLocal.adjcny_in[m] < this->blockWidth)
                          {
                            substEdit = SIMD::add ( nearbyColumns[graphLocal.adjcny_in[m] & (blockWidth-1)][l-1], sub512);
                            delEdit = SIMD::add ( nearbyColumns[graphLocal.adjcny_in[m] & (blockWidth-1)][l], del512);
                          }
                          else
                          {
                            substEdit = SIMD::add ( fartherColumns[graphLocal.adjcny_in[m]][l-1], sub512);
                            delEdit = SIMD::add ( fartherColumns[graphLocal.adjcny_in[m]][l], del512);
                          }

                          currentMax512 = SIMD::max (currentMax512, substEdit); 
                          currentMax512 = SIMD::max (currentMax512, delEdit); 
                        }

                        //insertion edit
                        __m512i insEdit = SIMD::add (nearbyColumns[k & (blockWidth-1)][l-1], ins512);
                        currentMax512 = SIMD::max (currentMax512, insEdit);
                      }

                      //update best score observed yet
                      bestScores512 = SIMD::max (currentMax512, bestScores512);

                      //on which lanes is the best score updated
                      auto updated = SIMD::cmpeq (currentMax512, bestScores512);

                      //update row and column values accordingly
                      bestRows512 = SIMD::mask_set1 (bestRows512, updated, (typename SIMD::type) (j + l));

                      if (colRegistersCountPerBatch == 1)
                      {
                        bestCols512_0 = SIMD::mask_set1_32 (bestCols512_0, updated,  (int32_t) k);
                      }
                      else if (colRegistersCountPerBatch == 2)
                      {
                        //lowest significant bits in 'updated' correspond to first set of reads
                        bestCols512_0 = SIMD::mask_set1_32 (bestCols512_0, updated ,  (int32_t) k);
                        bestCols512_1 = SIMD::mask_set1_32 (bestCols512_1, updated >> 1*colValuesPerRegister,  (int32_t) k);
                      }
                      else if (colRegistersCountPerBatch == 4)
                      {
                        bestCols512_0 = SIMD::mask_set1_32 (bestCols512_0, updated ,  (int32_t) k);
                        bestCols512_1 = SIMD::mask_set1_32 (bestCols512_1, updated >> 1*colValuesPerRegister,  (int32_t) k);
                        bestCols512_2 = SIMD::mask_set1_32 (bestCols512_2, updated >> 2*colValuesPerRegister,  (int32_t) k);
                        bestCols512_3 = SIMD::mask_set1_32 (bestCols512_3, updated >> 3*colValuesPerRegister,  (int32_t) k);
                      }

                      //save current score in small buffer
                      nearbyColumns[k & (blockWidth-1)][l] = currentMax512;

                      //save current score in large buffer if connected thru long hop
                      if ( withLongHopLocal[k] )
                        fartherColumns[k][l] = currentMax512;
                    }

                    //save last score for next row-wise iteration
                    lastBatchRow[loopJ & 1][k] = currentMax512; 

                  } // end of row computation
                } // end of DP

                bestScores[i] = bestScores512;
                bestRows[i]   = bestRows512; 
                {
                  //storing best columns requires extra work
                  if (colRegistersCountPerBatch == 1)
                  {
                    bestCols[1*i + 0]   = bestCols512_0; 
                  }
                  else if (colRegistersCountPerBatch == 2)
                  {
                    bestCols[2*i + 0]   = bestCols512_0;
                    bestCols[2*i + 1]   = bestCols512_1;
                  }
                  else if (colRegistersCountPerBatch == 4)
                  {
                    bestCols[4*i + 0]   = bestCols512_0;
                    bestCols[4*i + 1]   = bestCols512_1;
                    bestCols[4*i + 2]   = bestCols512_2;
                    bestCols[4*i + 3]   = bestCols512_3;
                  }
                }

              } // all reads done

              threadTimings[omp_get_thread_num()] = omp_get_wtime() - threadTimings[omp_get_thread_num()];

            } //end of omp parallel

            std::cout << "TIMER, psgl::alignToDAGLocal_Phase1_vectorized" 
                      << " (precision= " << sizeof(typename SIMD::type) << " bytes)" 
                      << ", individual thread timings (s) : " 
                      << printStats(threadTimings) << "\n"; 

#ifdef VTUNE_SUPPORT
            __itt_pause();
#endif
          }
    };

  /**
   * @brief   Supports phase 1 DP in reverse direction
   */
  template <typename SIMD>
    class Phase1_Rev_Vectorized
    {
      private:

        //reference graph
        const CSR_char_container &graph;

        // pre-compute which graph vertices are connected with hop longer
        // than 'blockWidth'
        std::vector<bool> withLongHop;

        //for converting input reads into SOA to enable vectorization
        std::vector<char> readSetSOA;

        //cumulative read batch sizes
        std::vector<size_t>  readSetSOAPrefixSum;

        //input reads
        const std::vector<std::string> &readSet;

        //read lengths in their 'sorted order'
        std::vector<size_t> sortedReadLengths;

        //sorted permutation order of input reads
        std::vector<size_t> sortedReadOrder;

      public:

        //small temporary storage buffer for DP scores
        //should be a power of 2
        static constexpr size_t blockWidth = Phase1_Vectorized<SIMD>::blockWidth; 

        //process these many vertical cells in a go
        //should be a power of 2
        static constexpr size_t blockHeight = Phase1_Vectorized<SIMD>::blockHeight;   

        /**
         * @brief                   public constructor
         * @param[in]   readSet     vector of input query sequences to align
         * @param[in]   g           input reference graph
         */
        Phase1_Rev_Vectorized(const std::vector<std::string> &readSet, 
            const CSR_char_container &g) :
          readSet (readSet), graph (g)
        {
          this->sortReadsForLoadBalance();
          this->convertToSOA();
          this->computeLongHops();
        };

        /**
         * @brief                                 wrapper function for phase 1 reverse DP routine 
         * @param[out]  outputBestScoreVector     vector to keep value and begin location of best scores,
         *                                        vector size is same as count of the reads
         * @note                                  this class won't care about rev. complement of sequences
         */
        template <typename Vec>
          void alignToDAGLocal_Phase1_rev_vectorized_wrapper(Vec &outputBestScoreVector) const
          {
            assert (outputBestScoreVector.size() == readSet.size());

            std::size_t countReadBatches = std::ceil (readSet.size() * 1.0 / SIMD::numSeqs);

            //column location value requires int32_t type, so may need >1 register per read batch
            constexpr size_t colValuesPerRegister = SIMD_REG_SIZE / (8 * sizeof(int32_t));
            constexpr size_t colRegistersCountPerBatch = SIMD::numSeqs / colValuesPerRegister;

            static_assert ( colRegistersCountPerBatch == 1 || 
                            colRegistersCountPerBatch == 2 || 
                            colRegistersCountPerBatch == 4); 

            // modified containers to hold best-score info
            // vector elements aligned to 64 byte boundaries
            std::vector <__m512i, aligned_allocator<__m512i, 64> > _bestScoreVector (countReadBatches);
            std::vector <__m512i, aligned_allocator<__m512i, 64> > _bestScoreRowVector (countReadBatches);
            std::vector <__m512i, aligned_allocator<__m512i, 64> > 
                  _bestScoreColVector (countReadBatches * colRegistersCountPerBatch);

            this->alignToDAGLocal_Phase1_rev_vectorized (outputBestScoreVector, _bestScoreVector, _bestScoreColVector, _bestScoreRowVector); 

            //when debugging for low-precision
#ifdef DEBUG
            for (auto &e : _bestScoreVector)
              simdUtils::print_avx_num16 (e);

            for (auto &e : _bestScoreColVector)
              simdUtils::print_avx_num32 (e);

            for (auto &e : _bestScoreRowVector)
              simdUtils::print_avx_num16 (e);
#endif

            //parse best scores from vector registers
            std::vector<typename SIMD::type, aligned_allocator<typename SIMD::type, 64> > storeScores (SIMD::numSeqs);
            std::vector<typename SIMD::type, aligned_allocator<typename SIMD::type, 64> > storeRows   (SIMD::numSeqs);
            std::vector<int32_t,             aligned_allocator<int32_t,             64> > storeCols   (SIMD::numSeqs);

            for (size_t i = 0; i < countReadBatches; i++)
            {
              SIMD::store (storeScores.data(), _bestScoreVector[i]);
              SIMD::store (storeRows.data(), _bestScoreRowVector[i]);

              for (size_t j = 0; j < colRegistersCountPerBatch; j++) 
              {
                SIMD::store (&storeCols [j*colValuesPerRegister], 
                            _bestScoreColVector[i * colRegistersCountPerBatch + j]);
              }

              for (size_t j = 0; j < SIMD::numSeqs; j++)
              {
                if (i * SIMD::numSeqs + j < readSet.size())
                {
                  auto originalReadId = sortedReadOrder[i * SIMD::numSeqs + j];

#ifdef DEBUG
                  std::cout << "INFO, psgl::Phase1_Vectorized::alignToDAGLocal_Phase1_rev_vectorized_wrapper, read # " << originalReadId << ",  score = " << storeScores[j] << ", refColumnStart = " << storeCols[j] << ", qryRowStart = " << readSet[originalReadId].length() - 1 - storeRows[j] << "\n";
#endif

                  assert (outputBestScoreVector[originalReadId].score == storeScores[j] - 1);  //offset by 1
                  outputBestScoreVector[originalReadId].refColumnStart  = storeCols[j];
                  outputBestScoreVector[originalReadId].qryRowStart     = readSet[originalReadId].length() - 1 - storeRows[j];

                }
              }
            }
          }

      private:

        /**
         * @brief       compute the sorted order of sequences for load balancing
         * @details     sorting is done in decreasing length order
         */
        void sortReadsForLoadBalance()
        {
          typedef std::pair<size_t, size_t> pair_t;

          //vector of tuples of length and index of reads
          std::vector<pair_t> lengthTuples;
           
          for(size_t i = 0; i < this->readSet.size(); i++)
            lengthTuples.emplace_back (readSet[i].length(), i);

          //sort in descending order, longer reads first
          std::sort (lengthTuples.begin(), lengthTuples.end(), std::greater<pair_t>() );

          for(auto &e : lengthTuples)
          {
            this->sortedReadLengths.push_back (e.first);
            this->sortedReadOrder.push_back (e.second);
          }
        }

        /**
         * @brief       convert input reads characters into SOA to enable vectorization
         */
        void convertToSOA()
        {
          assert (readSet.size() > 0);
          assert (sortedReadOrder.size() == readSet.size());
          assert (readSetSOA.size() == 0);

          /**
           * Requirements from the padding process
           * - each read length be a multiple of 'blockHeight'
           * - count of reads be a multiple of SIMD::numSeqs
           * - each batch of SIMD::numSeqs reads should be of equal length
           */

          //re-arrange read characters for vectorized processing; 
          //SIMD::numSeqs reads at a time, sorted in decreasing order by length
          
          auto readCount = readSet.size();

          readSetSOAPrefixSum.push_back(0);

          for (size_t i = 0; i < readCount; i += SIMD::numSeqs)
          {
            auto batchLength = readSet[sortedReadOrder[i]].length();  //longest read in this batch
            batchLength += blockHeight - 1 - (batchLength - 1) % blockHeight; //round-up

            for (size_t j = 0; j < batchLength; j++)
              for (size_t k = 0; k < SIMD::numSeqs; k++)
                if ( i + k < readCount && j < readSet[sortedReadOrder[i+k]].length() )
                  readSetSOA.push_back ( readSet[sortedReadOrder[i + k]][j] );
                else
                  readSetSOA.push_back (DUMMY);

            readSetSOAPrefixSum.push_back (readSetSOA.size());
          }

          std::size_t countReadBatches = std::ceil (readSet.size() * 1.0 / SIMD::numSeqs);
          assert (readSetSOAPrefixSum.size() == countReadBatches + 1);
        }

        /**
         * @brief     precompute which vertices are connected with long edge hops
         *            i.e. longer than 'blockWidth'
         */
        void computeLongHops()
        {
          assert (withLongHop.size() == 0);

          this->withLongHop.resize(graph.numVertices, false);

          for(int32_t i = 0; i < graph.numVertices; i++)
          {
            for(auto j = graph.offsets_in[i]; j < graph.offsets_in[i+1]; j++)
            {
              auto from_pos = graph.adjcny_in[j];
              auto to_pos = i;

              //compare hop distance to 'blockWidth'
              if (to_pos - from_pos >= this->blockWidth)
                this->withLongHop[to_pos] = true;
            }
          }

#ifdef DEBUG
          auto trueCount = std::count(withLongHop.begin(), withLongHop.end(), true);
          std::cout << "INFO, psgl::Phase1_Rev_Vectorized::computeLongHops, fraction of hops that are long: " << trueCount * 1.0 / graph.numVertices << "\n";
#endif
        }

        /**
         * @brief                               execute reverse variant of first phase of alignment i.e. 
         *                                      compute reverse DP and find begin locations of the best 
         *                                      alignment of each read
         * @param[in]   outputBestScoreVector   best scores and end locations computed during forward DP
         * @param[out]  bestScores              best DP scores of reads
         * @param[out]  bestCols                columns where best alignment starts
         * @param[out]  bestRows                rows where best alignment starts
         */
        template <typename Vec1, typename Vec2>
          void alignToDAGLocal_Phase1_rev_vectorized (const Vec1 &outputBestScoreVector,
                                                      Vec2 &bestScores, Vec2 &bestCols, Vec2 &bestRows) const
          {
            std::size_t readCount = readSet.size();
            std::size_t countReadBatches = std::ceil (readCount * 1.0 / SIMD::numSeqs);

            //column location value requires int32_t type, so may need >1 register per read batch
            constexpr size_t colValuesPerRegister = SIMD_REG_SIZE / (8 * sizeof(int32_t));
            constexpr size_t colRegistersCountPerBatch = SIMD::numSeqs / colValuesPerRegister;

            static_assert ( colRegistersCountPerBatch == 1 || 
                            colRegistersCountPerBatch == 2 || 
                            colRegistersCountPerBatch == 4); 

            //few checks
            assert (bestScores.size() == countReadBatches);
            assert (bestRows.size()   == countReadBatches);
            assert (bestCols.size()   == countReadBatches * colRegistersCountPerBatch);

//#ifdef VTUNE_SUPPORT
            //__itt_resume();
//#endif

            //init best score vector to zero bits
            std::fill (bestScores.begin(), bestScores.end(), SIMD::zero() );
            std::fill (bestCols.begin(),   bestCols.end(),   SIMD::zero() );
            std::fill (bestRows.begin(),   bestRows.end(),   SIMD::zero() );

            //init score simd vectors
            __m512i match512    = SIMD::set1 ((typename SIMD::type) SCORE::match);
            __m512i mismatch512 = SIMD::set1 ((typename SIMD::type) SCORE::mismatch);
            __m512i del512      = SIMD::set1 ((typename SIMD::type) SCORE::del);
            __m512i ins512      = SIMD::set1 ((typename SIMD::type) SCORE::ins);

            std::vector<double> threadTimings (omp_get_max_threads(), 0);

#pragma omp parallel
            {
#pragma omp barrier
              threadTimings[omp_get_thread_num()] = omp_get_wtime();

              //create local copy of graph for faster access
              const CSR_char_container graphLocal = this->graph;
              const std::vector<bool> withLongHopLocal = withLongHop;

              //type def. for memory-aligned vector allocation for SIMD instructions
              using AlignedVecType = std::vector <__m512i, aligned_allocator<__m512i, 64> >;

              //buffer to save selected columns (associated with long hops) of DP matrix
              std::size_t countLongHops = std::count (withLongHopLocal.begin(), withLongHopLocal.end(), true);
              AlignedVecType fartherColumnsBuffer (countLongHops * this->blockHeight);

              //for convenient access to 2D buffer
              std::vector<__m512i*> fartherColumns(graphLocal.numVertices);
              {
                size_t j = 0;

                for(int32_t i = 0; i < graphLocal.numVertices; i++)
                  if ( this->withLongHop[i] )
                    fartherColumns[i] = &fartherColumnsBuffer[ (j++) * this->blockHeight ];
              }

              //buffer to save neighboring column scores
              AlignedVecType nearbyColumnsBuffer (this->blockWidth * this->blockHeight);

              //for convenient access to 2D buffer
              std::vector<__m512i*> nearbyColumns (this->blockWidth);
              {
                for (std::size_t i = 0; i < this->blockWidth; i++)
                  nearbyColumns[i] = &nearbyColumnsBuffer[i * this->blockHeight];
              }

              //buffer to save scores of last row in each iteration
              //one row for writing and one for reading
              AlignedVecType lastBatchRowBuffer (2 * graphLocal.numVertices);

              //for convenient access to 2D buffer
              std::vector<__m512i*> lastBatchRow (2);
              {
                lastBatchRow[0] = &lastBatchRowBuffer[0];
                lastBatchRow[1] = &lastBatchRowBuffer[graphLocal.numVertices];
              }

              //buffer to save read charactes for innermost loop
              std::vector<typename SIMD::type, aligned_allocator<typename SIMD::type, 64> > readCharsInt (SIMD::numSeqs * this->blockHeight);

              //process SIMD::numSeqs reads in a single iteration
#pragma omp for schedule(dynamic) nowait
              for (size_t i = 0; i < countReadBatches; i++)
              {
                //first parse alignment locations of forward DP
                __m512i fwdBestRows512;

                //we may need at most 4 registers to save fwd col values
                __m512i fwdBestCols512_0;
                __m512i fwdBestCols512_1;
                __m512i fwdBestCols512_2;
                __m512i fwdBestCols512_3;

                //begin reading fwd DP results
                {
                  std::vector<typename SIMD::type, aligned_allocator<typename SIMD::type, 64> > fwdBestRows (SIMD::numSeqs);
                  std::vector<int32_t, aligned_allocator<int32_t, 64> > fwdBestCols (SIMD::numSeqs);

                  for (size_t j = 0; j < SIMD::numSeqs; j++)
                  {
                    if (i * SIMD::numSeqs + j < readSet.size())
                    {
                      auto originalReadId = sortedReadOrder[i * SIMD::numSeqs + j];
                      fwdBestCols[j] = outputBestScoreVector[originalReadId].refColumnEnd;
                      fwdBestRows[j] = readSet[originalReadId].length() - 1 - outputBestScoreVector[originalReadId].qryRowEnd;
                    }
                  }

                  fwdBestRows512 = SIMD::load ( fwdBestRows.data() );

                  {
                    fwdBestCols512_0   = SIMD::load (&fwdBestCols [0] ); 

                    //storing best columns requires extra work
                    if (colRegistersCountPerBatch >= 2)
                      fwdBestCols512_1   = SIMD::load (&fwdBestCols [1*colValuesPerRegister]); 

                    if (colRegistersCountPerBatch == 4)
                    {
                      fwdBestCols512_2   = SIMD::load (&fwdBestCols [2*colValuesPerRegister]); 
                      fwdBestCols512_3   = SIMD::load (&fwdBestCols [3*colValuesPerRegister]); 
                    }
                  }
                }

                __m512i bestScores512 = SIMD::zero();
                __m512i bestRows512   = SIMD::zero();

                //we may need at most 4 registers to save column for each batch (depending on SIMD::type)
                __m512i bestCols512_0   = SIMD::zero();
                __m512i bestCols512_1   = SIMD::zero();
                __m512i bestCols512_2   = SIMD::zero();
                __m512i bestCols512_3   = SIMD::zero();

                //reset DP 'lastBatchRow' buffer
                std::fill (lastBatchRowBuffer.begin(), lastBatchRowBuffer.end(), SIMD::zero() );

                int32_t qryBatchLength = readSet[sortedReadOrder[i * SIMD::numSeqs]].length();  //longest read in this batch
                qryBatchLength += this->blockHeight - 1 - (qryBatchLength - 1) % this->blockHeight; //round-up

                //iterate over read length (process more than 1 characters in batch)
                for (int32_t j = 0; j < qryBatchLength; j += this->blockHeight)
                {
                  //loop counter 
                  size_t loopJ = j / (this->blockHeight);

                  //convert read character to int32_t
                  for (int32_t k = 0; k < SIMD::numSeqs * this->blockHeight ; k++)
                  {
                    readCharsInt [k] = readSetSOA [readSetSOAPrefixSum[i] + j * SIMD::numSeqs + k];
                  }

                  //iterate over characters in reference graph
                  for (int32_t k = graphLocal.numVertices - 1; k >= 0; k--)
                  {
                    //current reference character
                    __m512i graphChar = SIMD::set1 ((typename SIMD::type) graphLocal.vertex_label[k] );

                    //current best score, init to 0
                    __m512i currentMax512;

                    //iterate over 'blockHeight' read characters
                    for (size_t l = 0; l < this->blockHeight; l++)
                    {
                      //load read characters
                      __m512i readChars = SIMD::load ( &readCharsInt[l * SIMD::numSeqs] );

                      //current best score, init to 0
                      currentMax512 = SIMD::zero();

                      //see if query and reference character match
                      auto compareChar = SIMD::cmpeq (readChars, graphChar);
                      __m512i sub512 = SIMD::blend (compareChar, mismatch512, match512);

                      //match-mismatch edit
                      currentMax512 = SIMD::max (currentMax512, sub512); //local alignment can also start with a match at this char 

                      //iterate over graph neighbors
                      //which buffers to access depends on the value of 'l'
                      if (l == 0)
                      {
                        for(size_t m = graphLocal.offsets_out[k]; m < graphLocal.offsets_out[k+1]; m++)
                        {
                          //paths with match mismatch edit
                          __m512i substEdit = SIMD::add ( lastBatchRow[(loopJ - 1) & 1][ graphLocal.adjcny_out[m] ], sub512);
                          currentMax512 = SIMD::max (currentMax512, substEdit); 

                          //paths with deletion edit
                          __m512i delEdit;

                          if (graphLocal.adjcny_out[m] - k < this->blockWidth)
                            delEdit = SIMD::add ( nearbyColumns[graphLocal.adjcny_out[m] & (blockWidth-1)][l], del512);
                          else
                            delEdit = SIMD::add ( fartherColumns[graphLocal.adjcny_out[m]][l], del512);

                          currentMax512 = SIMD::max (currentMax512, delEdit); 
                        }

                        //insertion edit
                        __m512i insEdit = SIMD::add (lastBatchRow[(loopJ - 1) & 1][k], ins512);
                        currentMax512 = SIMD::max (currentMax512, insEdit);
                      }
                      else
                      {
                        for(size_t m = graphLocal.offsets_out[k]; m < graphLocal.offsets_out[k+1]; m++)
                        {
                          //paths with match mismatch edit
                          __m512i substEdit;

                          //paths with deletion edit
                          __m512i delEdit;

                          if (graphLocal.adjcny_out[m] - k < this->blockWidth)
                          {
                            substEdit = SIMD::add ( nearbyColumns[graphLocal.adjcny_out[m] & (blockWidth-1)][l-1], sub512);
                            delEdit = SIMD::add ( nearbyColumns[graphLocal.adjcny_out[m] & (blockWidth-1)][l], del512);
                          }
                          else
                          {
                            substEdit = SIMD::add ( fartherColumns[graphLocal.adjcny_out[m]][l-1], sub512);
                            delEdit = SIMD::add ( fartherColumns[graphLocal.adjcny_out[m]][l], del512);
                          }

                          currentMax512 = SIMD::max (currentMax512, substEdit); 
                          currentMax512 = SIMD::max (currentMax512, delEdit); 
                        }

                        //insertion edit
                        __m512i insEdit = SIMD::add (nearbyColumns[k & (blockWidth-1)][l-1], ins512);
                        currentMax512 = SIMD::max (currentMax512, insEdit);
                      }

                      //update best score observed yet
                      bestScores512 = SIMD::max (currentMax512, bestScores512);

                      //on which lanes is the best score updated
                      auto updated = SIMD::cmpeq (currentMax512, bestScores512);

                      //update row and column values accordingly
                      bestRows512 = SIMD::mask_set1 (bestRows512, updated, (typename SIMD::type) (j + l));

                      if (colRegistersCountPerBatch == 1)
                      {
                        bestCols512_0 = SIMD::mask_set1_32 (bestCols512_0, updated,  (int32_t) k);
                      }
                      else if (colRegistersCountPerBatch == 2)
                      {
                        bestCols512_0 = SIMD::mask_set1_32 (bestCols512_0, updated,  (int32_t) k);
                        bestCols512_1 = SIMD::mask_set1_32 (bestCols512_1, updated >> 1*colValuesPerRegister,  (int32_t) k);
                      }
                      else if (colRegistersCountPerBatch == 4)
                      {
                        bestCols512_0 = SIMD::mask_set1_32 (bestCols512_0, updated,  (int32_t) k);
                        bestCols512_1 = SIMD::mask_set1_32 (bestCols512_1, updated >> 1*colValuesPerRegister,  (int32_t) k);
                        bestCols512_2 = SIMD::mask_set1_32 (bestCols512_2, updated >> 2*colValuesPerRegister,  (int32_t) k);
                        bestCols512_3 = SIMD::mask_set1_32 (bestCols512_3, updated >> 3*colValuesPerRegister,  (int32_t) k);
                      }

                      //detect and manipulate the score of the specific optimal alignment we want
                      //this is required to make sure reverse DP reports same alignment as fwd DP
                      {
                        __m512i currentRow = SIMD::set1 ( (typename SIMD::type) (j + l));
                        __m512i currentCol = SIMD::set1_32 ( (int32_t) k ); 
                        
                        auto compareCell = SIMD::cmpeq (fwdBestRows512, currentRow);

                        if (colRegistersCountPerBatch == 1)
                        {
                          auto compareCellByCol_0 = SIMD::cmpeq_32 (fwdBestCols512_0, currentCol);
                          compareCell = compareCell & 
                            ( compareCellByCol_0 << 0*colValuesPerRegister);
                        }
                        else if (colRegistersCountPerBatch == 2)
                        {
                          auto compareCellByCol_0 = SIMD::cmpeq_32 (fwdBestCols512_0, currentCol);
                          auto compareCellByCol_1 = SIMD::cmpeq_32 (fwdBestCols512_1, currentCol);

                          compareCell = compareCell & 
                            ( compareCellByCol_0 << 0*colValuesPerRegister | 
                              compareCellByCol_1 << 1*colValuesPerRegister);
                        }
                        else if (colRegistersCountPerBatch == 4)
                        {
                          auto compareCellByCol_0 = SIMD::cmpeq_32 (fwdBestCols512_0, currentCol);
                          auto compareCellByCol_1 = SIMD::cmpeq_32 (fwdBestCols512_1, currentCol);
                          auto compareCellByCol_2 = SIMD::cmpeq_32 (fwdBestCols512_2, currentCol);
                          auto compareCellByCol_3 = SIMD::cmpeq_32 (fwdBestCols512_3, currentCol);

                          compareCell = compareCell & 
                            ( compareCellByCol_0 << 0*colValuesPerRegister | 
                              compareCellByCol_1 << 1*colValuesPerRegister |
                              compareCellByCol_2 << 2*colValuesPerRegister |
                              compareCellByCol_3 << 3*colValuesPerRegister);
                        }

                        currentMax512 = SIMD::mask_set1 (currentMax512, compareCell, (typename SIMD::type) (SCORE::match + 1)); 
                      }

                      //save current score in small buffer
                      nearbyColumns[k & (blockWidth-1)][l] = currentMax512;

                      //save current score in large buffer if connected thru long hop
                      if ( withLongHopLocal[k] )
                        fartherColumns[k][l] = currentMax512;
                    }

                    //save last score for next row-wise iteration
                    lastBatchRow[loopJ & 1][k] = currentMax512; 

                  } // end of row computation
                } // end of DP

                bestScores[i] = bestScores512;
                bestRows[i]   = bestRows512; 
                {
                  //storing best columns requires extra work
                  if (colRegistersCountPerBatch == 1)
                  {
                    bestCols[1*i + 0]   = bestCols512_0; 
                  }
                  else if (colRegistersCountPerBatch == 2)
                  {
                    bestCols[2*i + 0]   = bestCols512_0;
                    bestCols[2*i + 1]   = bestCols512_1;
                  }
                  else if (colRegistersCountPerBatch == 4)
                  {
                    bestCols[4*i + 0]   = bestCols512_0;
                    bestCols[4*i + 1]   = bestCols512_1;
                    bestCols[4*i + 2]   = bestCols512_2;
                    bestCols[4*i + 3]   = bestCols512_3;
                  }
                }
              } // all reads done

              threadTimings[omp_get_thread_num()] = omp_get_wtime() - threadTimings[omp_get_thread_num()];

            } //end of omp parallel

            std::cout << "TIMER, psgl::alignToDAGLocal_Phase1_rev_vectorized" 
                      << " (precision= " << sizeof(typename SIMD::type) << " bytes)" 
                      << ", individual thread timings (s) : " 
                      << printStats(threadTimings) << "\n"; 

//#ifdef VTUNE_SUPPORT
            //__itt_pause();
//#endif
          }
    };
}

#endif
