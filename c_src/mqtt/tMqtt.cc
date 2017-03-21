#include <iostream>
#include <unistd.h>

#include "LevelManager.h"
#include "MosClient.h"
#include "ExceptionUtils.h"

using namespace nifutil;

int main(void)
{
    MosClient::toggleLogging(true);
    MosClient::startCommsLoop();
    MosClient::blockForever();
    
    return 0;
}
