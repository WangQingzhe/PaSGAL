/**
 * @file    align.hpp
 * @brief   routines to perform alignment
 * @author  Chirag Jain <cjain7@gatech.edu>
 */


#ifndef GRAPH_ALIGN_HPP
#define GRAPH_ALIGN_HPP

#include "graphLoad.hpp"
#include "csr.hpp"
#include "graph_iter.hpp"
#include "base_types.hpp"
#include "utils.hpp"

//External includes
#include "kseq.h"
#include "prettyprint.hpp"

KSEQ_INIT(gzFile, gzread)

namespace psgl
{

  /**
   * @brief                   local alignment routine
   * @tparam[in]  ScoreType   type to store scores in DP matrix
   * @param[in]   reads
   * @param[in]   graph
   */
  template <typename ScoreType, typename VertexIdType, typename EdgeIdType>
    void alignToDAGLocal( const std::vector<std::string> &reads,
        const CSR_container<VertexIdType, EdgeIdType> &graph)
    {

      //iterate over reads
#pragma omp parallel for
      for (size_t readno = 0; readno < reads.size(); readno++)
      {
        std::string read = reads[readno];

        float time_p1, time_p2, time_p3, time_p4;

        //
        // PHASE 1 : COMPUTE COMPLETE DP MATRIX
        //

        BestScoreInfo<ScoreType, VertexIdType> bestFwd;
        BestScoreInfo<ScoreType, VertexIdType> bestRev;

        std::string read_revComp (read);
        psgl::seqUtils::reverseComplement( read, read_revComp); 

        {
          auto tick1 = psgl::timer::rdtsc();

          //align read to ref.
          alignToDAGLocal_Phase1_Score (read, graph, bestFwd);

          //align reverse complement of read to ref.
          alignToDAGLocal_Phase1_Score (read_revComp, graph, bestRev);

          auto tick2 = psgl::timer::rdtsc();
          time_p1 = (tick2 -tick1) * 1.0/ psgl::timer::cycles_per_sec();
        }

        BestScoreInfo<ScoreType, VertexIdType> &best = bestFwd;

        if (bestFwd.score > bestRev.score)
        {
          best = bestFwd;
          best.strand = '+';
        }
        else
        {
          best = bestRev;
          best.strand = '-';
          read = read_revComp; 
        }

        //
        // PHASE 2 : COMPUTE FARTHEST REACHABLE VERTEX 
        //

        VertexIdType leftMostReachable;

        {
          auto tick1 = psgl::timer::rdtsc();

          std::size_t maxDistance = read.length() + std::ceil( read.length() * 1.0 * SCORE::match/SCORE::del );
          leftMostReachable = graph.computeLeftMostReachableVertex(best.vid, maxDistance);  

#ifdef DEBUG
          std::cout << "INFO, psgl::alignToDAGLocal, left most reachable vertex id = " << leftMostReachable << std::endl;
#endif

          auto tick2 = psgl::timer::rdtsc();
          time_p2 = (tick2 -tick1) * 1.0/ psgl::timer::cycles_per_sec();
        }

        //
        // PHASE 3 : RECOMPUTE DP MATRIX WITH TRACEBACK INFORMATION
        //

        //width of score matrix that we need in memory
        std::size_t reducedWidth = graph.totalRefLength(leftMostReachable, best.vid);

        //for new beginning column
        std::size_t j0; 

        //height of scoring matrix for re-computation
        std::size_t reducedHeight = best.qryRow + 1;   //i.e. row id ass. with best score -- plus one 

        //scores in the last row
        std::vector<ScoreType> finalRow(reducedWidth, 0);

        //complete score matrix of size height x width to allow traceback
        //Note: to optimize storge, we only store vertical difference; absolute values of 
        //      which is bounded by gap penalty
        std::vector< std::vector<int8_t> > completeMatrixLog(reducedHeight, std::vector<int8_t>(reducedWidth, 0));

        {
          auto tick1 = psgl::timer::rdtsc();

          //scoring matrix of size 2 x width, init with zero
          std::vector<std::vector<ScoreType>> matrix(2, std::vector<ScoreType>(reducedWidth, 0));

          //iterate over characters in read
          for (std::size_t i = 0; i < reducedHeight; i++)
          {
            //Iterate over reference graph, starting from 'leftMostReachable' vertex
            graphIterFwd <VertexIdType, EdgeIdType> g(graph, leftMostReachable);
            j0 = g.getGlobalOffset();   //beginning column

            //iterate over characters in reference graph
            for (std::size_t j = 0; j < reducedWidth; j++)
            {
              //current reference character
              char curChar = g.curChar();

              //insertion edit
              ScoreType fromInsertion = matrix[(i-1) % 2][j] - SCORE::ins;

              //get preceeding dependency offsets from graph
              std::vector<std::size_t> preceedingOffsets;
              g.getNeighborOffsets(preceedingOffsets);

              //match-mismatch edit
              ScoreType matchScore = curChar == read[i] ? SCORE::match : -1 * SCORE::mismatch;

              ScoreType fromMatch = matchScore;   //also handles the case when in-degree is zero 
              for(auto k : preceedingOffsets)
              {
                if (k >= j0)
                  fromMatch = std::max (fromMatch, matrix[(i-1) % 2][k-j0] + matchScore);
              }

              //deletion edit
              ScoreType fromDeletion  = -1; 
              for(auto k : preceedingOffsets)
              {
                if (k >= j0)
                  fromDeletion = std::max (fromDeletion, matrix[i % 2][k-j0] - SCORE::del);
              }

              //evaluate current score
              matrix[i % 2][j] = std::max ( std::max(fromInsertion, fromMatch) , std::max(fromDeletion, 0) );

              //save vertical difference of scores, used later for backtracking
              completeMatrixLog[i][j] = matrix[i % 2][j] - matrix[(i-1) % 2][j];

              //advance graph iterator
              g.next();
            }

            //Save last row
            if (i == reducedHeight - 1) 
              finalRow = matrix[i % 2];
          }

          ScoreType bestScoreReComputed = *std::max_element(finalRow.begin(), finalRow.end());

          //the recomputed score and its location should match our original calculation
          assert( bestScoreReComputed == best.score );
          assert( bestScoreReComputed == finalRow[ best.refColumn - j0 ] );

          auto tick2 = psgl::timer::rdtsc();
          time_p3 = (tick2 -tick1) * 1.0/ psgl::timer::cycles_per_sec();
        }

        //
        // PHASE 4 : COMPUTE CIGAR
        //
        
        std::string cigar;

        {
          auto tick1 = psgl::timer::rdtsc();

          //iterate over graph in reverse direction
          //we shall move from bottom (best scoring cell) to up
          graphIterRev <VertexIdType, EdgeIdType> g(graph, best);

          std::vector<ScoreType> currentRowScores = finalRow; 
          std::vector<ScoreType> aboveRowScores (reducedWidth);

          int col = g.getGlobalOffset() - j0;
          int row = best.qryRow;

          while (col >= 0 && row >= 0)
          {
            if (currentRowScores[col] <= 0)
              break;

            //retrieve score values from vertical score differences
            for(std::size_t i = 0; i < reducedWidth; i++)
              aboveRowScores[i] = currentRowScores[i] - completeMatrixLog[row][i]; 

            //current reference character
            char curChar = g.curChar();

            //insertion edit
            ScoreType fromInsertion = aboveRowScores[col] - SCORE::ins;

            //get preceeding dependency offsets from graph
            std::vector<std::size_t> preceedingOffsets;
            g.getNeighborOffsets(preceedingOffsets);

            //match-mismatch edit
            ScoreType matchScore = curChar == read[row] ? SCORE::match : -1 * SCORE::mismatch;

            ScoreType fromMatch = matchScore;   //also handles the case when in-degree is zero 
            std::size_t fromMatchPos = g.getGlobalOffset();

            for(auto k : preceedingOffsets)
            {
              if (k >= j0 && fromMatch < aboveRowScores[k-j0] + matchScore)
              {
                fromMatch = aboveRowScores[k-j0] + matchScore;
                fromMatchPos = k;
              }
            }

            //deletion edit
            ScoreType fromDeletion = -1; 
            std::size_t fromDeletionPos;

            for(auto k : preceedingOffsets)
            {
              if (k >= j0 && fromDeletion <  currentRowScores[k-j0] - SCORE::del)
              {
                fromDeletion = currentRowScores[k-j0] - SCORE::del;
                fromDeletionPos = k;
              }
            }

            //evaluate recurrence
            {
              if (currentRowScores[col] == fromMatch)
              {
                if (matchScore == SCORE::match)
                  cigar.push_back('=');
                else
                  cigar.push_back('X');

                //if alignment starts from this column, stop
                if (fromMatchPos == g.getGlobalOffset())
                  break;

                //shift to preceeding offset
                g.jump(fromMatchPos);

                //shift to above row
                row--; currentRowScores = aboveRowScores;
              }
              else if (currentRowScores[col] == fromDeletion)
              {
                cigar.push_back('D');

                //shift to preceeding offset
                g.jump(fromDeletionPos);
              }
              else 
              {
                assert(currentRowScores[col] == fromInsertion);

                cigar.push_back('I');

                //shift to above row
                row--; currentRowScores = aboveRowScores;
              }
            }

            col = g.getGlobalOffset() - j0;
          }

          //string reverse 
          std::reverse (cigar.begin(), cigar.end());  

          //shorten the cigar string
          psgl::seqUtils::cigarCompact(cigar);

          //validate if cigar yields best score
          assert ( psgl::seqUtils::cigarScore<ScoreType> (cigar) ==  best.score );

          auto tick2 = psgl::timer::rdtsc();
          time_p4 = (tick2 -tick1) * 1.0/ psgl::timer::cycles_per_sec();
        }

#pragma omp critical 
        {
          std::cout << "INFO, psgl::alignToDAGLocal, aligning read #" << readno + 1 << ", length = " << read.length() << std::endl;
          std::cout << "INFO, psgl::alignToDAGLocal, best score = " << best.score << ", strand = " << best.strand << ", ending at vertex id = " << best.vid << ", DP row = " << best.qryRow << ", DP col = " << best.refColumn << std::endl;
          std::cout << "INFO, psgl::alignToDAGLocal, cigar: " << cigar << std::endl;
          std::cout << "TIMER, psgl::alignToDAGLocal, phase timings (sec) : " << time_p1 << ", " << time_p2 << ", " << time_p3 << ", " << time_p4 << std::endl;
        }
      }
    }

  /**
   * @brief                   local alignment routine
   * @tparam[in]  ScoreType   type to store scores in DP matrix
   * @param[in]   read
   * @param[in]   readLength
   * @param[in]   graph
   * @param[out]  best        value and location of best score
   */
  template <typename ScoreType, typename VertexIdType, typename EdgeIdType>
    void alignToDAGLocal_Phase1_Score(  const std::string &read,
                                        const CSR_container<VertexIdType, EdgeIdType> &graph,
                                        BestScoreInfo<ScoreType, VertexIdType> &best)
    {
      //width of score matrix that we need in memory
      auto width = graph.totalRefLength();

      //initialize matrix of size 2 x width, init with zero
      //we will keep re-using rows to keep memory-usage low
      std::vector< std::vector<ScoreType> > matrix(2, std::vector<ScoreType>(width, 0));

      //iterate over characters in read
      for (std::size_t i = 0; i < read.length(); i++)
      {
        //iterate over characters in reference graph
        for (graphIterFwd <VertexIdType, EdgeIdType> g(graph); !g.end(); g.next())
        {
          //current reference character
          char curChar = g.curChar();

          //current column number in DP matrix
          std::size_t j = g.getGlobalOffset();

          //insertion edit
          ScoreType fromInsertion = matrix[(i-1) % 2][j] - SCORE::ins;

          //get preceeding dependency offsets from graph
          std::vector<std::size_t> preceedingOffsets;
          g.getNeighborOffsets(preceedingOffsets);

          //match-mismatch edit
          ScoreType matchScore = curChar == read[i] ? SCORE::match : -1 * SCORE::mismatch;

          ScoreType fromMatch = matchScore;   //local alignment can also start with a match at this char
          for(auto k : preceedingOffsets)
          {
            fromMatch = std::max (fromMatch, matrix[(i-1) % 2][k] + matchScore);
          }

          //deletion edit
          ScoreType fromDeletion = -1; 
          for(auto k : preceedingOffsets)
          {
            fromDeletion = std::max (fromDeletion, matrix[i % 2][k] - SCORE::del);
          }

          //Evaluate recursion 
          matrix[i % 2][j] = std::max ( std::max(fromInsertion, fromMatch) , std::max(fromDeletion, 0) );

          //Update best score observed till now
          if (best.score < matrix[i % 2][j])
          {
            best.score = matrix[i % 2][j];
            best.vid = g.getCurrentVertexId();
            best.vertexSeqOffset = g.getCurrentSeqOffset();
            best.refColumn = j;
            best.qryRow = i;
          }
        }
      }
    }

  /**
   * @brief                   alignment routine
   * @tparam[in]  ScoreType   type to store scores in DP matrix
   * @param[in]   reads       vector of strings
   * @param[in]   graph
   * @param[in]   mode
   */
  template <typename ScoreType, typename VertexIdType, typename EdgeIdType>
    void alignToDAG(const std::vector<std::string> &reads, 
        const CSR_container<VertexIdType, EdgeIdType> &graph,
        const MODE mode)  
    {
      static_assert(std::is_signed<ScoreType>::value, 
          "ERROR, psgl::alignToDAG, ScoreType must be a signed type");

      switch(mode)
      {
        //case GLOBAL : alignToDAGGlobal<ScoreType> (reads, graph); break;
        case LOCAL : alignToDAGLocal<ScoreType> (reads, graph); break;
        //case SEMIGLOBAL: alignToDAGSemiGlobal<ScoreType> (reads, graph); break;
        default: std::cerr << "ERROR, psgl::alignToDAG, Invalid alignment mode"; exit(1);
      }
    }

  /**
   * @brief                   alignment routine
   * @tparam[in]  ScoreType   type to store scores in DP matrix
   * @param[in]   qfile       file name containing reads
   * @param[in]   graph
   * @param[in]   mode
   */
  template <typename ScoreType, typename VertexIdType, typename EdgeIdType>
    void alignToDAG(const std::string &qfile, 
        const CSR_container<VertexIdType, EdgeIdType> &graph,
        const MODE mode)  
    {
      //Parse all reads into a vector
      std::vector<std::string> reads;

      {
        //Open the file using kseq
        FILE *file = fopen (qfile.c_str(), "r");
        gzFile fp = gzdopen (fileno(file), "r");
        kseq_t *seq = kseq_init(fp);

        //size of sequence
        int len;

        while ((len = kseq_read(seq)) >= 0) 
        {
          psgl::seqUtils::makeUpperCase(seq->seq.s, len);

          reads.push_back(seq->seq.s);
        }

        //Close the input file
        kseq_destroy(seq);  
        gzclose(fp);  
      }

//#ifdef DEBUG
        std::cout << "INFO, psgl::alignToDAG, total count of reads = " << reads.size() << std::endl;
//#endif

      alignToDAG<ScoreType> (reads, graph, mode);
    }
}

#endif