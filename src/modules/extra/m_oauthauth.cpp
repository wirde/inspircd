/*
 * Authentication module for Lightbulb Crew OAuth2.
 * Takes:
 *       nick on the format uXaY where X is the userId and Y is the avatarId
 *       password an access_token which is valid for user/avatar X/Y
 */

#include "inspircd.h"
#include <curl/curl.h>
#include <curl/easy.h>

/* $ModDesc: Allow/Deny connections based on an OAuth2 tokens and a userId/avatarId combination */
/* $LinkerFlags: -lcurl */

enum AuthState {
    AUTH_STATE_NONE = 0,
    AUTH_STATE_BUSY = 1,
    AUTH_STATE_FAIL = 2
};

static size_t callback_func(char* stream, size_t size, size_t nmemb, void* userPtr)
{
    size_t realsize = size*nmemb;
    std::string* userStr = static_cast<std::string*>(userPtr);
    userStr->append(stream, realsize);
    return realsize;
}

class ModuleOauthAuth : public Module
{
    LocalIntExt pendingExt;

    std::string killreason;
    std::string identityServer;
    std::string worldServer;

 public:
    ModuleOauthAuth() : pendingExt("oauthauth-wait", this)
    {
    }

    void init()
    {
        CURLcode curlInit = curl_global_init(CURL_GLOBAL_ALL);
        if (curlInit) {
             throw ModuleException("Unable to initialize curl in m_oauthauth.");
        }
        
        ServerInstance->Modules->AddService(pendingExt);
        OnRehash(NULL);
        Implementation eventlist[] = { I_OnCheckReady, I_OnRehash, I_OnUserRegister };
        ServerInstance->Modules->Attach(eventlist, this, sizeof(eventlist)/sizeof(Implementation));
    }

    void OnRehash(User* user)
    {
        ConfigTag* conf = ServerInstance->Config->ConfValue("oauthauth");
        killreason = conf->getString("killreason");
        identityServer = conf->getString("identityServer");
        worldServer = conf->getString("worldServer");
    }

    ModResult OnUserRegister(LocalUser* user)
    {
        if (pendingExt.get(user))
            return MOD_RES_PASSTHRU;

        std::string toParse  = user->password;

        size_t uPos = toParse.find_first_of('&');

        if (uPos == std::string::npos)
        {
            //Pass not in correct format
            pendingExt.set(user, AUTH_STATE_FAIL);
            ServerInstance->Logs->Log("MODULE", DEBUG, "uPos failed");
            return MOD_RES_PASSTHRU;
        } 
        uPos++;
        std::string userId = toParse.substr(0, uPos - 1);
        size_t aPos = toParse.find_first_of('&', uPos);

        if (aPos == std::string::npos)
        {
            //Pass not in correct format
            pendingExt.set(user, AUTH_STATE_FAIL);
            ServerInstance->Logs->Log("MODULE", DEBUG, "aPos failed");
            return MOD_RES_PASSTHRU;
        } 
        aPos++;
        std::string avatarId = toParse.substr(uPos, (aPos - 1) - uPos);
        std::string token = toParse.substr(aPos, std::string::npos);
        if (token.empty())
        {
            //Pass not in correct format
            pendingExt.set(user, AUTH_STATE_FAIL);
            ServerInstance->Logs->Log("MODULE", DEBUG, "token not found");
            ServerInstance->Logs->Log("MODULE", DEBUG, "token = " + token + " avatardId = " + avatarId);
            return MOD_RES_PASSTHRU;
        }

        //TODO: The remote calls must be changed to be non-blocking at some point for performance!
        bool validToken = checkToken(identityServer, userId, avatarId, token);
        if (validToken) 
        {
            pendingExt.set(user, AUTH_STATE_NONE);
            //Change nick to the displayName
            std::string displayName = fetchDisplayName(worldServer, avatarId);
            if (displayName != "")
            {
                user->ChangeNick(displayName);
            }
            else
            {
                pendingExt.set(user, AUTH_STATE_FAIL);
                ServerInstance->Logs->Log("MODULE", DEBUG, "changeNick failed");
                return MOD_RES_PASSTHRU;
            }
        } 
        else 
        {
            ServerInstance->Logs->Log("MODULE", DEBUG, "token failed");
            pendingExt.set(user, AUTH_STATE_FAIL);
        }        

        return MOD_RES_PASSTHRU;
    }

    ModResult OnCheckReady(LocalUser* user)
    {
        switch (pendingExt.get(user))
        {
            case AUTH_STATE_NONE:
                return MOD_RES_PASSTHRU;
            case AUTH_STATE_BUSY:
                return MOD_RES_DENY;
            case AUTH_STATE_FAIL:
                ServerInstance->Users->QuitUser(user, killreason);
                return MOD_RES_DENY;
        }
        return MOD_RES_PASSTHRU;
    }

    Version GetVersion()
    {
        return Version("Allow/Deny connections based on an OAuth2 tokens and a userId/avatarId combination", VF_VENDOR);
    }
    
    private:
    //TODO: Return the name that is to be used?
    //TODO: Test with SSL
    bool checkToken(const std::string& baseUrl, const std::string& userId, const std::string& avatarId, const std::string& token) {
        std::string url = baseUrl;
        url += "/oauth2/users/";
        url += userId;
        url += "/avatars/";
        url += avatarId;
        
        std::string authHeader = "Authorization: Bearer ";
        authHeader += token;
        
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, authHeader.c_str());  
    
        CURL *curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);        
            curl_easy_perform(curl);
            long statusCode;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
            /* always cleanup */
            curl_easy_cleanup(curl);
            if (statusCode > 200 && statusCode < 299)
                return true;        
        }
        return false;
    }

    std::string fetchDisplayName(const std::string& baseUrl, const std::string& avatarId)
    {
        std::string url = baseUrl;
        url += "/avatars/";
        url += avatarId;
        url += "/name";

        CURL *curl = curl_easy_init();
        if (curl) 
        {
            std::string displayName;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &displayName);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &callback_func);
            curl_easy_perform(curl);
            long statusCode;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
            //DisplayName is returned as a "json string" - enclosed with quotes
            if (statusCode < 200 || statusCode > 299 || displayName.length() <= 2)
                return "";
                
            displayName.erase(0,1);
            displayName.erase(displayName.size() - 1,1);
            curl_easy_cleanup(curl);
            return displayName;
        }
        return "";
    }
};

MODULE_INIT(ModuleOauthAuth)