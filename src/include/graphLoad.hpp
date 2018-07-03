/**
 * @file    graphLoad.hpp
 * @brief   routines to load DNA sequence graph from various file formats 
 * @author  Chirag Jain <cjain7@gatech.edu>
 */

#ifndef GRAPH_LOADER_HPP
#define GRAPH_LOADER_HPP

#include <cassert>
#include <iostream>


//Own includes
#include "csr.hpp"
#include "stream.hpp"
#include "vg.pb.h"

namespace psgl
{

  /**
   * @brief                       supports loading of sequence graphs from VG file format
   * @tparam[in]    VertexIdType  data type to store vertex ids (tbd by total #vertices)
   * @tparam[in]    EdgeIdType    data type to store offsets in CSR graph (tbd by total #edges)
   */
  template <typename VertexIdType = uint32_t, typename EdgeIdType = uint32_t>
    class graphLoader
    {
      public:

        //initialize an empty di-graph 
        CSR_container<VertexIdType, EdgeIdType> diGraph;

        /**
         * @brief                 load graph from VG graph format
         * @param[in]  filename
         * @details               VG tool (https://github.com/vgteam/vg) uses .vg format to save graphs
         */
        void loadFromVG(const std::string &filename)
        {
          if(!exists(filename))
          {
            std::cerr << filename << " not accessible." << std::endl;
            exit(1);
          }

          //Read vertices in the graph 
          {
            std::ifstream graphFile {filename, std::ios::in | std::ios::binary};

            std::function<void(vg::Graph&)> lambda_v = [this](vg::Graph& g) {

              //Get total count of vertices
              diGraph.addVertexCount(g.node_size());

              for (int i = 0; i < g.node_size(); i++)
              {
                auto vg_vertex = g.node(i);

                //add vertex to diGraph
                //vertex numbering in vg starts from 1, so adjust accordingly
                diGraph.initVertexSequence(vg_vertex.id() - 1, vg_vertex.sequence());
              }
            };

            stream::for_each(graphFile, lambda_v);
          }

          //Read edges in the graph 
          {
            std::ifstream graphFile {filename, std::ios::in | std::ios::binary};

            std::vector <std::pair <VertexIdType, VertexIdType> > edgeVector;

            std::function<void(vg::Graph&)> lambda_e = [&edgeVector](vg::Graph& g) {
              for (int i = 0; i < g.edge_size(); i++)
              {
                auto vg_edge = g.edge(i);

                //todo: add support for bi-directed graphs
                assert(("Bi-directed graph not supported yet", vg_edge.from_start() == false));
                assert(("Bi-directed graph not supported yet", vg_edge.to_end() == false));
                assert(("Graph overlaps not supported yet", vg_edge.overlap() == 0));

                //add edge to diGraph
                //vertex numbering in vg starts from 1, so adjust accordingly
                edgeVector.emplace_back(vg_edge.from() - 1, vg_edge.to() - 1);
              }
            };

            stream::for_each(graphFile, lambda_e);

            diGraph.initEdges(edgeVector);
          }

          //topological sort
          this->sortVerify();
        }

        /**
         *  @brief                  load graph from .txt file
         *  @param[in]  filename
         *  @details                file format: 
         *                          first line specifies count of vertices
         *                          following lines specify out-neighbors and label 
         *                          for each vertex delimited by spaces (one vertex per line)
         */
        void loadFromTxt(const std::string &filename)
        {
          if(!exists(filename))
          {
            std::cerr << filename << " not accessible." << std::endl;
            exit(1);
          }

          std::string line;
          std::ifstream infile(filename);

          VertexIdType totalVertices;
          std::vector <std::pair <VertexIdType, VertexIdType> > edgeVector;

          int currentRow = 0;

          while (std::getline(infile, line))
          {
            std::istringstream inputString(line);

            //get count of vertices from header row
            if (currentRow == 0)
            {
              inputString >> totalVertices;
              diGraph.addVertexCount(totalVertices);
            }
            else //get out-neighbor vertex ids and vertex label
            {
              assert(currentRow <= totalVertices);

              //Parse the input line
              std::vector<std::string> tokens (std::istream_iterator<std::string>{inputString}, std::istream_iterator<std::string>());
              assert(tokens.size() > 0);

              diGraph.initVertexSequence (currentRow - 1, tokens.back());

              for (auto it = tokens.begin(); it != tokens.end() && std::next(it) != tokens.end(); it++)
              {
                edgeVector.emplace_back (currentRow - 1, stoi(*it));
              }
            }

            currentRow++;
          }

          diGraph.initEdges(edgeVector);

          assert (diGraph.numVertices > 0);
          assert (diGraph.numEdges > 0);

          //topological sort
          this->sortVerify();
        }

        /**
         * @brief     print the loaded directed graph to stderr 
         */
        void printGraph() const
        {
          diGraph.printGraph();
        }

      private:

        /**
         * @brief   topologically sort the graph and verify correctness
         */
        void sortVerify()
        {
          //Topological sort
          diGraph.sort();

#ifndef NDEBUG
          //verify correctness of CSR container
          diGraph.verify();
#endif
        }

        /**
         * @brief     check if file is accessible
         */
        bool exists(const std::string &filename) const
        {
          std::ifstream infile(filename);
          return infile.good();
        }
    };

}

#endif
