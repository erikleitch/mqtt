#include <iostream>
#include <unistd.h>

#include "LevelManager.h"
#include "MosClient.h"
#include "ExceptionUtils.h"

#include "String.h"

using namespace nifutil;

int main(void)
{
    gcp::util::String str("varchar, varchar, varchar]");

    gcp::util::String atom;
    do {
        atom = str.findNextStringSeparatedByChars("[,]");
        COUT("Atom = " << atom);
    } while(!atom.isEmpty());
    
    MosClient::toggleLogging(true);
    MosClient::startCommsLoop();
    MosClient::blockForever();
    
    return 0;
}
