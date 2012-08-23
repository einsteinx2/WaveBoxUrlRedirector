#include "fcgiapp.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sqlite3.h>
#include <unistd.h>

void test_envs(FCGX_Stream* out, FCGX_ParamArray envp);
void print_env(FCGX_Stream* out, FCGX_ParamArray envp, const char* name);
void grab_envs(FCGX_ParamArray envp, char** remote_ip, char** host, char** request_uri, char** request_method, char** query_string);
void not_found(FCGX_Stream* out);
void unimplemented(FCGX_Stream* out);
void temp_redirect(FCGX_Stream* out, char* ip, int port, char* uri);
void get_reg_params(char* query_string, char** new_host, char** new_internal, int* new_port, char** new_server_id, long long* new_timestamp);
bool is_local_ip(const char* ip);
bool info_for_host(sqlite3* db, char* host, char** internal, char** external, int* port, char** server_id, long long* timestamp);
bool register_url(sqlite3* db, const char* url, const char* int_ip, const char* ext_ip, int port, const char *server_id, long long timestamp);

// The file name of the sqlite3 database
#define DB_PATH "test.db"

// This is the URL that handles registrations. When this URL is accessed
// instead of forwarding, we look for the url query parameters and
// attempt to register a new url for this user, or update an existing one
#define REG_URL "register.benjamm.in"

// Test print FastCGI environment variables
void test_envs(FCGX_Stream* out, FCGX_ParamArray envp)
{
    FCGX_FPrintF(out, "Content-type: text/plain\r\n\r\n");
    print_env(out, envp, "REQUEST_URI");
    print_env(out, envp, "REMOTE_ADDR");
    print_env(out, envp, "REQUEST_METHOD");
    print_env(out, envp, "SERVER_NAME");
    print_env(out, envp, "SERVER_PORT");
    print_env(out, envp, "SERVER_PROTOCOL");
    print_env(out, envp, "QUERY_STRING");
}

// Print a specific FastCGI environment variable
void print_env(FCGX_Stream* out, FCGX_ParamArray envp, const char* name)
{
    char *var = FCGX_GetParam(name, envp);
    if (var == NULL)
        FCGX_FPrintF(out, "%s doesn't exist\r\n", name);
    else
        FCGX_FPrintF(out, "%s: %s\r\n", name, var);
}

// Grab the needed FastCGI environment variables
void grab_envs(FCGX_ParamArray envp, char** remote_ip, char** host, char** request_uri, char** request_method, char** query_string)
{
    *remote_ip = FCGX_GetParam("REMOTE_ADDR", envp);
    *host = FCGX_GetParam("SERVER_NAME", envp);
    *request_uri = FCGX_GetParam("REQUEST_URI", envp);
    *request_method = FCGX_GetParam("REQUEST_METHOD", envp);
    *query_string = FCGX_GetParam("QUERY_STRING", envp);
}

// Send the registration success message
void reg_success(FCGX_Stream* out)
{
    FCGX_FPrintF(out,"Content-type: application/json\r\n"
                     "\r\n"
                     "{\"success\":true\r\n}");
}

// Send the registration failure message
void reg_failure(FCGX_Stream* out, char* error_msg)
{
    FCGX_FPrintF(out,"Content-type: application/json\r\n"
                     "\r\n"
                     "{\"success\":false,\"error\":\"%s\"", error_msg);
}

// Send the not found header, used for unregistered URLs
void not_found(FCGX_Stream* out)
{    
    FCGX_FPrintF(out, "Status: 404 Not Found\r\n"
                      "Content-type: text/html\r\n"
                      "\r\n");
}

// Send the not implemented header, used for request types other 
// than GET and POST
void unimplemented(FCGX_Stream* out)
{    
    FCGX_FPrintF(out, "Status: 501 Method Not Implemented\r\n"
                      "Allow: GET, POST\r\n"
                      "Content-type: text/html\r\n"
                      "\r\n");
}

// Perform a 302 redirect to the user's address
void temp_redirect(FCGX_Stream* out, char* ip, int port, char* uri)
{
    char location[1024];
    if (port == 80)
        sprintf(location, "%s%s", ip, uri);
    else
        sprintf(location, "%s:%d%s", ip, port, uri);
    
    FCGX_FPrintF(out, "Status: 302 Moved Temporarily\r\n"
                      "Content-Length: 0\r\n"
                      "Location: %s\r\n"
                      "Connection: close\r\n\r\n",
                      location);
}

// Get the stored information for the requested host name, so
// we can perform the redirect
bool info_for_host(sqlite3* db, char* host, char** internal, char** external, int* port, char** server_id, long long* timestamp)
{
    bool success = false;
    
    sqlite3_stmt *stmt;
    char query[512];
    sprintf(query, "SELECT * FROM urls WHERE url = '%s'", host);

    int rc = sqlite3_prepare_v2(db, query, -1, &stmt, 0);
    if(rc)
    {
        fprintf(stderr, "SQL error: %d : %s\n", rc, sqlite3_errmsg(db));
    }
    else
    {
        // execute the statement
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW)
        {
            char *buf;
            
            // Internal IP
            buf = (char *)sqlite3_column_text(stmt, 1);
            strcpy(*internal, buf);
            
            // External IP
            buf = (char *)sqlite3_column_text(stmt, 2);
            strcpy(*external, buf);
            
            // Port
            *port = sqlite3_column_int(stmt, 3);
            
            // Server ID
            buf = (char *)sqlite3_column_text(stmt, 4);
            strcpy(*server_id, buf);
            
            // UNIX Timestamp
            *timestamp = sqlite3_column_int64(stmt, 5);

            success = true;
        }

        // finalize the statement to release resources
        sqlite3_finalize(stmt);
    }

    return success;
}

// Parse the registration parameters from the URL query string
void get_reg_params(char* query_string, char** new_host, char** new_internal, int* new_port, char** new_server_id, long long* new_timestamp)
{
    char s[1024];
    char *outside;
    char *inside;

    strcpy(s, query_string);
    char *key_val = strtok_r(s, "&", &outside);

    while (key_val) 
    {
        char *key = strtok_r(key_val, "=", &inside);
        char *val = strtok_r(NULL, "=", &inside);

        if (strcasecmp(key, "url") == 0)
        {
            strcpy(*new_host, val);
        }
        else if (strcasecmp(key, "local_ip") == 0)
        {
            strcpy(*new_internal, val);
        }
        else if (strcasecmp(key, "port") == 0)
        {
            *new_port = atoi(val);
        }
        else if (strcasecmp(key, "server_id") == 0)
        {
            *new_server_id = strcpy(*new_server_id, val);
        }
        else if (strcasecmp(key, "timestamp") == 0)
        {
            *new_timestamp = atoll(val);
        }

        key_val = strtok_r(NULL, "&", &outside);
    }
}

// Save a new url registration
bool register_url(sqlite3* db, const char* url, const char* int_ip, const char* ext_ip, int port, const char *server_id, long long timestamp)
{
    char query[512];
    sprintf(query, "REPLACE INTO urls VALUES ('%s', '%s', '%s', %i, '%s', %lld)", url, int_ip, ext_ip, port, server_id, timestamp);

    int rc = sqlite3_exec(db, query, 0, 0, 0);
    if (rc)
    {
        fprintf(stderr, "Can't save registration info: %s\n", sqlite3_errmsg(db));
        return false;
    }
    else
    {
        return true;
    }
}

// Open the Sqlite3 database
bool open_db(sqlite3** db)
{
    int rc = sqlite3_open(DB_PATH, db);
    if(rc)
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(*db));
        return false;
    }
    else
    {
        return true;
    }
}

// Close the Sqlite3 database
bool close_db(sqlite3* db)
{
    int rc = sqlite3_close(db);
    if(rc)
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return false;
    }
    else
    {
        return true;
    }
}

int main(void)
{
    // FastCGI stuff
    FCGX_Stream *in, *out, *err;
    FCGX_ParamArray envp;

    // Sqlite3 stuff
    sqlite3 *db = NULL;

    // Loop through the available FastCGI requests
    while (FCGX_Accept(&in, &out, &err, &envp) >= 0) 
    {
        // Open the database
        open_db(&db);

        // Grab environment
        char *remote_ip = NULL;
        char *host = NULL;
        char *request_uri = NULL;
        char *request_method = NULL;
        char *query_string = NULL;
        grab_envs(envp, &remote_ip, &host, &request_uri, &request_method, &query_string);

        // Make sure this is a GET or POST request
        if (strcasecmp(request_method, "GET") == 0 || strcasecmp(request_method, "POST") == 0)
        {
            char internal[16];    char *int_ptr = (char *)&internal;
            char external[16];    char *ext_ptr = (char *)&external;
            int port;
            char server_id[41];   char *sid_ptr = (char *)server_id;
            long long timestamp;
            
            bool exists = info_for_host(db, host, &int_ptr, &ext_ptr, &port, &sid_ptr, &timestamp);

            // Check if this is a registration
            if (strcasecmp(host, REG_URL) == 0)
            {
                // If this is a POST, get the query string from the POST body
                char body[1024];
                if (strcasecmp(request_method, "POST") == 0)
                {
                    FCGX_GetStr(body, sizeof(body), in);
                    query_string = (char *)&body;
                }

                // Get the query parameters
                char new_host[256];       char *new_host_ptr = (char *)&new_host;
                char new_internal[16];    char *new_int_ptr = (char *)&new_internal;
                int new_port;
                char new_server_id[41];   char *new_sid_ptr = (char *)&server_id;
                long long new_timestamp;

                get_reg_params(query_string, &new_host_ptr, &new_int_ptr, &new_port, &new_sid_ptr, &new_timestamp);

                // This is a registration, check if it exists in the db already
                if (!exists || (exists && strcasecmp(server_id, new_server_id) == 0))
                {
                    // This url is either not registered yet, or it's the same person updating their registration
                    if (register_url(db, new_host, new_internal, remote_ip, new_port, new_server_id, new_timestamp))
                        reg_success(out);
                    else
                        reg_failure(out, "Database error, please try again");
                }
                else
                {
                    reg_failure(out, "Registration already exists, choose a different hostname");
                }
            }
            else
            {
                if (exists)
                {
                    if (strcasecmp(external, remote_ip) == 0)
                    {
                        // This user is inside their home network
                        temp_redirect(out, internal, port, request_uri);
                    }
                    else
                    {
                        // This user is outside their home network
                        temp_redirect(out, external, port, request_uri);
                    }
                }
                else
                {
                    // Not found, so return an error
                    not_found(out);
                }
            }
        }
        else
        {
            // We can only handle GET and POST requests
            unimplemented(out);
        }

        close_db(db);
    }

    return 0;
}