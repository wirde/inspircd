/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  Inspire is copyright (C) 2002-2005 ChatSpike-Dev.
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

using namespace std;

#include <stdio.h>
#include <vector>
#include <deque>
#include "globals.h"
#include "inspircd_config.h"
#ifdef GCC3
#include <ext/hash_map>
#else
#include <hash_map>
#endif
#include "users.h"
#include "channels.h"
#include "modules.h"
#include "socket.h"
#include "helperfuncs.h"
#include "inspircd.h"
#include "inspstring.h"
#include "hashcomp.h"
#include "message.h"

#ifdef GCC3
#define nspace __gnu_cxx
#else
#define nspace std
#endif

enum ServerState { LISTENER, CONNECTING, WAIT_AUTH_1, WAIT_AUTH_2, CONNECTED };

typedef nspace::hash_map<std::string, userrec*, nspace::hash<string>, irc::StrHashComp> user_hash;
extern user_hash clientlist;

class TreeServer;
class TreeSocket;

class TreeServer
{
	TreeServer* Parent;
	std::vector<TreeServer*> Children;
	std::string ServerName;
	std::string ServerDesc;
	std::string VersionString;
	int UserCount;
	int OperCount;
	TreeSocket* Socket;	// for directly connected servers this points at the socket object
	
 public:

	TreeServer()
	{
		Parent = NULL;
		ServerName = "";
		ServerDesc = "";
		VersionString = "";
		UserCount = OperCount = 0;
	}

	TreeServer(std::string Name, std::string Desc) : ServerName(Name), ServerDesc(Desc)
	{
		Parent = NULL;
		VersionString = "";
		UserCount = OperCount = 0;
	}

	TreeServer(std::string Name, std::string Desc, TreeServer* Above, TreeSocket* Sock) : Parent(Above), ServerName(Name), ServerDesc(Desc), Socket(Sock)
	{
		VersionString = "";
		UserCount = OperCount = 0;
	}

	std::string GetName()
	{
		return this->ServerName;
	}

	std::string GetDesc()
	{
		return this->ServerDesc;
	}

	std::string GetVersion()
	{
		return this->VersionString;
	}

	int GetUserCount()
	{
		return this->UserCount;
	}

	int GetOperCount()
	{
		return this->OperCount;
	}

	TreeSocket* GetSocket()
	{
		return this->Socket;
	}

	TreeServer* GetParent()
	{
		return this->Parent;
	}

	unsigned int ChildCount()
	{
		return Children.size();
	}

	TreeServer* GetChild(unsigned int n)
	{
		if (n < Children.size())
		{
			return Children[n];
		}
		else
		{
			return NULL;
		}
	}

	void AddChild(TreeServer* Child)
	{
		Children.push_back(Child);
	}

	bool DelChild(TreeServer* Child)
	{
		for (std::vector<TreeServer*>::iterator a = Children.begin(); a < Children.end(); a++)
		{
			if (*a == Child)
			{
				Children.erase(a);
				return true;
			}
		}
		return false;
	}
};

class Link
{
 public:
	 std::string Name;
	 std::string IPAddr;
	 int Port;
	 std::string SendPass;
	 std::string RecvPass;
};

/* $ModDesc: Povides a spanning tree server link protocol */

Server *Srv;
ConfigReader *Conf;
TreeServer *TreeRoot;
std::vector<Link> LinkBlocks;

TreeServer* RouteEnumerate(TreeServer* Current, std::string ServerName)
{
	if (Current->GetName() == ServerName)
		return Current;
	for (unsigned int q = 0; q < Current->ChildCount(); q++)
	{
		TreeServer* found = RouteEnumerate(Current->GetChild(q),ServerName);
		if (found)
		{
			return found;
		}
	}
	return NULL;
}

// Returns the locally connected server we must route a
// message through to reach server 'ServerName'. This
// only applies to one-to-one and not one-to-many routing.
TreeServer* BestRouteTo(std::string ServerName)
{
	log(DEBUG,"Finding best route to %s",ServerName.c_str());
	// first, find the server by recursively walking the tree
	TreeServer* Found = RouteEnumerate(TreeRoot,ServerName);
	// did we find it? If not, they did something wrong, abort.
	if (!Found)
	{
		log(DEBUG,"Failed to find %s by walking tree!",ServerName.c_str());
		return NULL;
	}
	else
	{
		// The server exists, follow its parent nodes until
		// the parent of the current is 'TreeRoot', we know
		// then that this is a directly-connected server.
		while ((Found) && (Found->GetParent() != TreeRoot))
		{
			Found = Found->GetParent();
		}
		log(DEBUG,"Route to %s is via %s",ServerName.c_str(),Found->GetName().c_str());
		return Found;
	}
}

class TreeSocket : public InspSocket
{
	std::string myhost;
	std::string in_buffer;
	ServerState LinkState;
	std::string InboundServerName;
	std::string InboundDescription;
	
 public:

	TreeSocket(std::string host, int port, bool listening, unsigned long maxtime)
		: InspSocket(host, port, listening, maxtime)
	{
		Srv->Log(DEBUG,"Create new listening");
		myhost = host;
		this->LinkState = LISTENER;
	}

	TreeSocket(std::string host, int port, bool listening, unsigned long maxtime, std::string ServerName)
		: InspSocket(host, port, listening, maxtime)
	{
		Srv->Log(DEBUG,"Create new outbound");
		myhost = ServerName;
		this->LinkState = CONNECTING;
	}

	TreeSocket(int newfd, char* ip)
		: InspSocket(newfd, ip)
	{
		Srv->Log(DEBUG,"Associate new inbound");
		this->LinkState = WAIT_AUTH_1;
	}
	
        virtual bool OnConnected()
	{
		if (this->LinkState == CONNECTING)
		{
			Srv->SendOpers("*** Connection to "+myhost+"["+this->GetIP()+"] established.");
			// we should send our details here.
			// if the other side is satisfied, they send theirs.
			// we do not need to change state here.
			for (std::vector<Link>::iterator x = LinkBlocks.begin(); x < LinkBlocks.end(); x++)
			{
				if (x->Name == this->myhost)
				{
					// found who we're supposed to be connecting to, send the neccessary gubbins.
					this->WriteLine("SERVER "+Srv->GetServerName()+" "+x->SendPass+" 0 :"+Srv->GetServerDescription());
					return true;
				}
			}
		}
		log(DEBUG,"Outbound connection ERROR: Could not find the right link block!");
		return true;
	}
	
        virtual void OnError(InspSocketError e)
	{
	}

        virtual int OnDisconnect()
	{
		return true;
	}

	// recursively send the server tree with distances as hops
	void SendServers(TreeServer* Current, TreeServer* s, int hops)
	{
		char command[1024];
		for (unsigned int q = 0; q < Current->ChildCount(); q++)
		{
			TreeServer* recursive_server = Current->GetChild(q);
			if (recursive_server != s)
			{
				// :source.server SERVER server.name hops :Description
				snprintf(command,1024,":%s SERVER %s * %d :%s",Current->GetName().c_str(),recursive_server->GetName().c_str(),hops,recursive_server->GetDesc().c_str());
				this->WriteLine(command);
				// down to next level
				this->SendServers(recursive_server, s, hops+1);
			}
		}
	}

	// send all users and their channels
	void SendUsers(TreeServer* Current)
	{
		char data[MAXBUF];
		for (user_hash::iterator u = clientlist.begin(); u != clientlist.end(); u++)
		{
			snprintf(data,MAXBUF,":%s NICK %lu %s %s %s %s +%s %s :%s",u->second->server,(unsigned long)u->second->age,u->second->nick,u->second->host,u->second->dhost,u->second->ident,u->second->modes,u->second->ip,u->second->fullname);
			this->WriteLine(data);
			if (strchr(u->second->modes,'o'))
			{
				this->WriteLine(":"+std::string(u->second->nick)+" OPERTYPE "+std::string(u->second->oper));
			}
			char* chl = chlist(u->second,u->second);
			if (*chl)
			{
				this->WriteLine(":"+std::string(u->second->nick)+" JOIN "+std::string(chl));
			}
		}
	}

	void DoBurst(TreeServer* s)
	{
		log(DEBUG,"Beginning network burst");
		Srv->SendOpers("*** Bursting to "+s->GetName()+".");
		this->WriteLine("BURST");
		// Send server tree
		this->SendServers(TreeRoot,s,1);
		// Send users and their channels
		this->SendUsers(s);
		// TODO: Send everything else
		this->WriteLine("ENDBURST");
	}

        virtual bool OnDataReady()
	{
		char* data = this->Read();
		if (data)
		{
			this->in_buffer += data;
			while (in_buffer.find("\n") != std::string::npos)
			{
				char* line = (char*)in_buffer.c_str();
				std::string ret = "";
			        while ((*line != '\n') && (strlen(line)))
				{
					ret = ret + *line;
					line++;
				}
				if ((*line == '\n') || (*line == '\r'))
					line++;
				in_buffer = line;
				if (!this->ProcessLine(ret))
				{
					return false;
				}
			}
		}
		return (data != NULL);
	}

	int WriteLine(std::string line)
	{
		return this->Write(line + "\r\n");
	}

	bool Error(std::deque<std::string> params)
	{
		if (params.size() < 1)
			return false;
		std::string Errmsg = params[0];
		std::string SName = myhost;
		if (InboundServerName != "")
		{
			SName = InboundServerName;
		}
		Srv->SendOpers("*** ERROR from "+SName+": "+Errmsg);
		// we will return false to cause the socket to close.
		return false;
	}

	bool Outbound_Reply_Server(std::deque<std::string> params)
	{
		if (params.size() < 4)
			return false;
		std::string servername = params[0];
		std::string password = params[1];
		int hops = atoi(params[2].c_str());
		if (hops)
		{
			this->WriteLine("ERROR :Server too far away for authentication");
			return false;
		}
		std::string description = params[3];
		Srv->SendToModeMask("o",WM_AND,"outbound-server-replied: name='"+servername+"' pass='"+password+"' description='"+description+"'");
		for (std::vector<Link>::iterator x = LinkBlocks.begin(); x < LinkBlocks.end(); x++)
		{
			if ((x->Name == servername) && (x->RecvPass == password))
			{
				// Begin the sync here. this kickstarts the
				// other side, waiting in WAIT_AUTH_2 state,
				// into starting their burst, as it shows
				// that we're happy.
				this->LinkState = CONNECTED;
				// we should add the details of this server now
				// to the servers tree, as a child of the root
				// node.
				TreeServer* Node = new TreeServer(servername,description,TreeRoot,this);
				TreeRoot->AddChild(Node);
				this->DoBurst(Node);
				return true;
			}
		}
		this->WriteLine("ERROR :Invalid credentials");
		return false;
	}

	bool Inbound_Server(std::deque<std::string> params)
	{
		if (params.size() < 4)
			return false;
		std::string servername = params[0];
		std::string password = params[1];
		int hops = atoi(params[2].c_str());
		if (hops)
		{
			this->WriteLine("ERROR :Server too far away for authentication");
			return false;
		}
		std::string description = params[3];
		for (std::vector<Link>::iterator x = LinkBlocks.begin(); x < LinkBlocks.end(); x++)
		{
			if ((x->Name == servername) && (x->RecvPass == password))
			{
				Srv->SendOpers("*** Verified incoming server connection from "+servername+"["+this->GetIP()+"] ("+description+")");
				this->InboundServerName = servername;
				this->InboundDescription = description;
				// this is good. Send our details: Our server name and description and hopcount of 0,
				// along with the sendpass from this block.
				this->WriteLine("SERVER "+Srv->GetServerName()+" "+x->SendPass+" 0 :"+Srv->GetServerDescription());
				// move to the next state, we are now waiting for THEM.
				this->LinkState = WAIT_AUTH_2;
				return true;
			}
		}
		this->WriteLine("ERROR :Invalid credentials");
		return false;
	}

	std::deque<std::string> Split(std::string line)
	{
		std::deque<std::string> n;
		std::stringstream s(line);
		std::string param = "";
		n.clear();
		int item = 0;
		while (!s.eof())
		{
			s >> param;
			if ((param.c_str()[0] == ':') && (item))
			{
				char* str = (char*)param.c_str();
				str++;
				param = str;
				std::string append;
				while (!s.eof())
				{
					append = "";
					s >> append;
					if (append != "")
					{
						param = param + " " + append;
					}
				}
			}
			item++;
			n.push_back(param);
		}
		return n;
	}

	bool ProcessLine(std::string line)
	{
		Srv->SendToModeMask("o",WM_AND,"inbound-line: '"+line+"'");

		std::deque<std::string> params = this->Split(line);
		std::string command = "";
		std::string prefix = "";
		if ((params[0].c_str())[0] == ':')
		{
			prefix = params[0];
			command = params[1];
			params.pop_front();
			params.pop_front();
		}
		else
		{
			prefix = "";
			command = params[0];
			params.pop_front();
		}
		
		switch (this->LinkState)
		{
			TreeServer* Node;
			
			case WAIT_AUTH_1:
				// Waiting for SERVER command from remote server. Server initiating
				// the connection sends the first SERVER command, listening server
				// replies with theirs if its happy, then if the initiator is happy,
				// it starts to send its net sync, which starts the merge, otherwise
				// it sends an ERROR.
				if (command == "SERVER")
				{
					return this->Inbound_Server(params);
				}
				else if (command == "ERROR")
				{
					return this->Error(params);
				}
			break;
			case WAIT_AUTH_2:
				// Waiting for start of other side's netmerge to say they liked our
				// password.
				if (command == "SERVER")
				{
					// cant do this, they sent it to us in the WAIT_AUTH_1 state!
					// silently ignore.
					return true;
				}
				else if (command == "BURST")
				{
					this->LinkState = CONNECTED;
					Node = new TreeServer(InboundServerName,InboundDescription,TreeRoot,this);
	                                TreeRoot->AddChild(Node);
	                                this->DoBurst(Node);
				}
				else if (command == "ERROR")
				{
					return this->Error(params);
				}
				
			break;
			case LISTENER:
				this->WriteLine("ERROR :Internal error -- listening socket accepted its own descriptor!!!");
				return false;
			break;
			case CONNECTING:
				if (command == "SERVER")
				{
					// another server we connected to, which was in WAIT_AUTH_1 state,
					// has just sent us their credentials. If we get this far, theyre
					// happy with OUR credentials, and they are now in WAIT_AUTH_2 state.
					// if we're happy with this, we should send our netburst which
					// kickstarts the merge.
					return this->Outbound_Reply_Server(params);
				}
			break;
			case CONNECTED:
				// This is the 'authenticated' state, when all passwords
				// have been exchanged and anything past this point is taken
				// as gospel.
				return true;
			break;	
		}
		return true;
	}

        virtual void OnTimeout()
	{
		if (this->LinkState = CONNECTING)
		{
			Srv->SendOpers("*** CONNECT: Connection to "+myhost+" timed out.");
		}
	}

        virtual void OnClose()
	{
	}

	virtual int OnIncomingConnection(int newsock, char* ip)
	{
		TreeSocket* s = new TreeSocket(newsock, ip);
		Srv->AddSocket(s);
		return true;
	}
};

class ModuleSpanningTree : public Module
{
	std::vector<TreeSocket*> Bindings;

 public:

	void ReadConfiguration(bool rebind)
	{
		if (rebind)
		{
			for (int j =0; j < Conf->Enumerate("bind"); j++)
			{
				std::string Type = Conf->ReadValue("bind","type",j);
				std::string IP = Conf->ReadValue("bind","address",j);
				long Port = Conf->ReadInteger("bind","port",j,true);
				if (Type == "servers")
				{
					if (IP == "*")
					{
						IP = "";
					}
					TreeSocket* listener = new TreeSocket(IP.c_str(),Port,true,10);
					if (listener->GetState() == I_LISTENING)
					{
						Srv->AddSocket(listener);
						Bindings.push_back(listener);
					}
					else
					{
						log(DEFAULT,"m_spanningtree: Warning: Failed to bind server port %d",Port);
						listener->Close();
						delete listener;
					}
				}
			}
		}
		LinkBlocks.clear();
		for (int j =0; j < Conf->Enumerate("link"); j++)
		{
			Link L;
			L.Name = Conf->ReadValue("link","name",j);
			L.IPAddr = Conf->ReadValue("link","ipaddr",j);
			L.Port = Conf->ReadInteger("link","port",j,true);
			L.SendPass = Conf->ReadValue("link","sendpass",j);
			L.RecvPass = Conf->ReadValue("link","recvpass",j);
			LinkBlocks.push_back(L);
			log(DEBUG,"m_spanningtree: Read server %s with host %s:%d",L.Name.c_str(),L.IPAddr.c_str(),L.Port);
		}
	}

	ModuleSpanningTree()
	{
		Srv = new Server;
		Conf = new ConfigReader;
		Bindings.clear();

		// Create the root of the tree
		TreeRoot = new TreeServer(Srv->GetServerName(),Srv->GetServerDescription());

		ReadConfiguration(true);
	}

	void HandleLinks(char** parameters, int pcnt, userrec* user)
	{
		return;
	}

	void HandleLusers(char** parameters, int pcnt, userrec* user)
	{
		return;
	}

	void HandleMap(char** parameters, int pcnt, userrec* user)
	{
		return;
	}

	int HandleSquit(char** parameters, int pcnt, userrec* user)
	{
		return 1;
	}

	int HandleConnect(char** parameters, int pcnt, userrec* user)
	{
		for (std::vector<Link>::iterator x = LinkBlocks.begin(); x < LinkBlocks.end(); x++)
		{
			if (Srv->MatchText(x->Name.c_str(),parameters[0]))
			{
				WriteServ(user->fd,"NOTICE %s :*** CONNECT: Connecting to server: %s (%s:%d)",user->nick,x->Name.c_str(),x->IPAddr.c_str(),x->Port);
				TreeSocket* newsocket = new TreeSocket(x->IPAddr,x->Port,false,10,x->Name);
				Srv->AddSocket(newsocket);
				return 1;
			}
		}
		WriteServ(user->fd,"NOTICE %s :*** CONNECT: No matching server could be found in the config file.",user->nick);
		return 1;
	}

	virtual int OnPreCommand(std::string command, char **parameters, int pcnt, userrec *user)
	{
		if (command == "CONNECT")
		{
			return this->HandleConnect(parameters,pcnt,user);
		}
		else if (command == "SQUIT")
		{
			return this->HandleSquit(parameters,pcnt,user);
		}
		else if (command == "MAP")
		{
			this->HandleMap(parameters,pcnt,user);
			return 1;
		}
		else if (command == "LUSERS")
		{
			this->HandleLusers(parameters,pcnt,user);
			return 1;
		}
		else if (command == "LINKS")
		{
			this->HandleLinks(parameters,pcnt,user);
			return 1;
		}
		return 0;
	}

	virtual ~ModuleSpanningTree()
	{
		delete Srv;
	}

	virtual Version GetVersion()
	{
		return Version(1,0,0,0,VF_STATIC|VF_VENDOR);
	}
};


class ModuleSpanningTreeFactory : public ModuleFactory
{
 public:
	ModuleSpanningTreeFactory()
	{
	}
	
	~ModuleSpanningTreeFactory()
	{
	}
	
	virtual Module * CreateModule()
	{
		return new ModuleSpanningTree;
	}
	
};


extern "C" void * init_module( void )
{
	return new ModuleSpanningTreeFactory;
}

