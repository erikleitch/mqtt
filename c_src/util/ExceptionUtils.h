#ifndef EXCEPTIONUTILS_H
#define EXCEPTIONUTILS_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>

#define OSTERM(os, term, head, tail, text)               \
{                                                        \
    if(term) {                                           \
        if(head)                                         \
            os << "\r";                                  \
        os << text;                                      \
        if(tail)                                         \
            os << "\r";                                  \
    } else {                                             \
        os << text;                                      \
    }                                                    \
}

#define GREEN "\033[32m"
#define RED   "\033[91m"
#define NORM  "\033[0m"

#define ThrowRuntimeError(text) {                       \
        std::ostringstream _macroOs;                    \
        _macroOs << text;                               \
        throw std::runtime_error(_macroOs.str());       \
    }

#define ThrowSysError(text) {                           \
        std::ostringstream _macroOs;                    \
        _macroOs << text;                               \
        throw std::runtime_error(_macroOs.str());       \
    }

#define COUT(text) {                                                    \
        std::ostringstream _macroOs;                                    \
        _macroOs << text;                                               \
        std::cout << '\r' << _macroOs.str() << std::endl << '\r' << std::endl; \
    }

#define COUTRED(text) {                                                 \
        std::ostringstream _macroOs;                                    \
        _macroOs << RED << text << NORM;                                \
        std::cout << '\r' << _macroOs.str() << std::endl << "\r";       \
    }

#define COUTGREEN(text) {                                               \
        std::ostringstream _macroOs;                                    \
        _macroOs << GREEN << text << NORM;                              \
        std::cout << '\r' << _macroOs.str() << std::endl;               \
    }

#define FOUT(text) {                                                    \
        std::fstream outfile;                                           \
        outfile.open("/tmp/riak_test_scratch/eleveldb.txt", std::fstream::out|std::fstream::app); \
        outfile << text << std::endl;                                   \
        outfile.close();                                                \
    }                                                                  
#endif
