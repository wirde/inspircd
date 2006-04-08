/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd is copyright (C) 2002-2006 ChatSpike-Dev.
 *                       E-mail:
 *                <brain@chatspike.net>
 *           	  <Craig@chatspike.net>
 *     
 * Written by Craig Edwards, Craig McLure, and others.
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#ifndef __INSPIRCD_H__
#define __INSPIRCD_H__

#include "inspircd_config.h"
#include <string>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#ifndef _LINUX_C_LIB_VERSION
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#endif

#include <arpa/inet.h>
#include <string>
#include <deque>
#include <sstream>

#include "users.h"
#include "channels.h"
#include "socket.h"
#include "mode.h"
#include "socketengine.h"
#include "command_parse.h"

// some misc defines
#define ERROR -1
#define MAXCOMMAND 32

// crucial defines
#define ETIREDGERBILS EAGAIN

// This define is used in place of strcmp when we 
// want to check if a char* string contains only one
// letter. Pretty fast, its just two compares and an
// addition.
#define IS_SINGLE(x,y) ( (*x == y) && (*(x+1) == 0) )

template<typename T> inline std::string ConvToStr(const T &in)
{
	std::stringstream tmp;
	if (!(tmp << in)) return std::string();
	return tmp.str();
}


class serverstats
{
  public:
	int statsAccept;
	int statsRefused;
	int statsUnknown;
	int statsCollisions;
	int statsDns;
	int statsDnsGood;
	int statsDnsBad;
	int statsConnects;
	int statsSent;
	int statsRecv;
	int BoundPortCount;

	serverstats()
	{
		statsAccept = statsRefused = statsUnknown = 0;
		statsCollisions = statsDns = statsDnsGood = 0;
		statsDnsBad = statsConnects = statsSent = statsRecv = 0;
		BoundPortCount = 0;
	}
};


class InspIRCd
{

 private:
	char MODERR[MAXBUF];
        void erase_factory(int j);
        void erase_module(int j);
	void BuildISupport();
	void MoveTo(std::string modulename,int slot);
        bool expire_run;

 public:
	time_t startup_time;
	ModeParser* ModeGrok;
	CommandParser* Parser;
	SocketEngine* SE;
	serverstats* stats;

	void MakeLowerMap();
	std::string GetRevision();
	std::string GetVersionString();
	char* ModuleError();
	bool LoadModule(const char* filename);
	bool UnloadModule(const char* filename);
	void MoveToLast(std::string modulename);
	void MoveToFirst(std::string modulename);
	void MoveAfter(std::string modulename, std::string after);
	void MoveBefore(std::string modulename, std::string before);
	InspIRCd(int argc, char** argv);
	void DoOneIteration(bool process_module_sockets);
	int Run();

};

/* Miscellaneous stuff here, moved from inspircd_io.h */
void Exit(int status); 
void Start(); 
void SetSignals();
bool DaemonSeed();
void WritePID(const std::string &filename);

/* userrec optimization stuff */
void AddServerName(std::string servername);
const char* FindServerNamePtr(std::string servername);
bool FindServerName(std::string servername);

#endif
