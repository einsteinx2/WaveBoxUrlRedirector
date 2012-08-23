#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include <unistd.h>
#include "fcgiapp.h"

void test_envs(FCGX_Stream *out, FCGX_ParamArray envp);
void print_env(FCGX_Stream *out, FCGX_ParamArray envp, const char *name);
void grab_envs(FCGX_ParamArray envp, char **remote_ip, char **host, char **request_uri, char **request_method, char **query_string);
void not_found(FCGX_Stream *out);
void unimplemented(FCGX_Stream *out);
void temp_redirect(FCGX_Stream *out, char *ip, int port, char *uri);
void get_reg_params(char *query_string, char **new_reg_url, char **new_reg_ip, int *new_reg_port);
int is_local_ip(const char *ip);
int ips_for_host(sqlite3 *db, char *host, char **internal, char **external, int *port);

#define DB_PATH "test.db"
#define REG_URL "register.benjamm.in"

void test_envs(FCGX_Stream *out, FCGX_ParamArray envp)
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

void grab_envs(FCGX_ParamArray envp, char **remote_ip, char **host, char **request_uri, char **request_method, char **query_string)
{
    *remote_ip = FCGX_GetParam("REMOTE_ADDR", envp);
    *host = FCGX_GetParam("SERVER_NAME", envp);
    *request_uri = FCGX_GetParam("REQUEST_URI", envp);
    *request_method = FCGX_GetParam("REQUEST_METHOD", envp);
    *query_string = FCGX_GetParam("QUERY_STRING", envp);
}

void print_env(FCGX_Stream *out, FCGX_ParamArray envp, const char *name)
{
    char *var = FCGX_GetParam(name, envp);
    if (var == NULL)
        FCGX_FPrintF(out, "%s doesn't exist\r\n", name);
    else
        FCGX_FPrintF(out, "%s: %s\r\n", name, var);
}

void not_found(FCGX_Stream *out)
{    
    // This causes bad gateway error
    FCGX_FPrintF(out, "HTTP/1.0 404 NOT FOUND\r\n");
}

void unimplemented(FCGX_Stream *out)
{    
    // This causes bad gateway error
    // Print the unimplemented header
    FCGX_FPrintF(out, "Allow: GET, POST\r\n"//"HTTP/1.0 501 Method Not Implemented\r\n");
                      "Content-Type: text/html\r\n");
                      //"\r\n");
}

void temp_redirect(FCGX_Stream *out, char *ip, int port, char *uri)
{        
    // Print the redirect header

    char location[1024];
    if (port == 80)
        sprintf(location, "%s%s", ip, uri);
    else
        sprintf(location, "%s:%d%s", ip, port, uri);

    // The first line is not actually necessary, FastCGI 
    // automatically inserts the HTTP status code when 
    // the Location line is sent 
    FCGX_FPrintF(out, "HTTP/1.1 302 Moved Temporarily\r\n"
                      "Content-Length: 0\r\n"
                      "Location: %s\r\n"
                      "Connection: close\r\n\r\n",
                      location);
}

int ips_for_host(sqlite3 *db, char *host, char **internal, char **external, int *port)
{
    int success = 0;
    
    sqlite3_stmt *stmt;

    char query[1024];
    //strcpy(query, "SELECT * FROM urls WHERE url = \"test.benjamm.in\"");
    strcpy(query, "SELECT * FROM urls WHERE url = '");
    strcat(query, host);
    strcat(query, "'");

    fprintf(stderr, "query: %s", query);

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
            // internal ip is column 1
            char *buf;
            buf = (char *)sqlite3_column_text(stmt, 1);
            strcpy(*internal, buf);
            buf = (char *)sqlite3_column_text(stmt, 2);
            strcpy(*external, buf);
            *port = sqlite3_column_int(stmt, 3);

            success = 1;
        }

        // finalize the statement to release resources
        sqlite3_finalize(stmt);
    }

    return success;
}

void get_reg_params(char *query_string, char **new_reg_url, char **new_reg_ip, int *new_reg_port)
{
    //FCGX_FPrintF(out,"Content-type: text/plain\r\n\r\n");
    char s[1024];
    char *outside;
    char *inside;

    strcpy(s, query_string);
    char *key_val = strtok_r(s, "&", &outside);

    while (key_val) 
    {
        //FCGX_FPrintF(out, "key value pair: %s\n", key_val);
        
        char *key = strtok_r(key_val, "=", &inside);
        char *val = strtok_r(NULL, "=", &inside);
        //FCGX_FPrintF(out, "key: %s  value: %s\n", key, val);

        if (strcasecmp(key, "url") == 0)
        {
            strcpy(*new_reg_url, val);
        }
        else if (strcasecmp(key, "local_ip") == 0)
        {
            strcpy(*new_reg_ip, val);
        }
        else if (strcasecmp(key, "port") == 0)
        {
            *new_reg_port = atoi(val);
        }

        key_val = strtok_r(NULL, "&", &outside);
    }
}

int register_url(sqlite3 *db, const char *url, const char *int_ip, const char *ext_ip)
{
    char query[1024];
    strcpy(query, "INSERT INTO urls VALUES (\"");
    strcat(query, url);
    strcat(query, "\",\"");
    strcat(query, int_ip);
    strcat(query, "\",\"");
    strcat(query, ext_ip);
    strcat(query, "\")");

    int rc = sqlite3_exec(db, query, 0, 0, 0);
    if (rc)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

int open_db(sqlite3 **db)
{
    int rc = sqlite3_open(DB_PATH, db);
    if(rc)
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(*db));
        return 0;
    }
    else
    {
        return 1;
    }
}

int close_db(sqlite3 *db)
{
    int rc = sqlite3_close(db);
    if(rc)
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    else
    {
        return 1;
    }
}

int main(void)
{
    // FastCGI stuff
    FCGX_Stream *in, *out, *err;
    FCGX_ParamArray envp;

    // Sqlite3 stuff
    sqlite3 *db = NULL;

    while (FCGX_Accept(&in, &out, &err, &envp) >= 0) 
    {
        open_db(&db);

        // Grab environment
        char *remote_ip = NULL;
        char *host = NULL;
        char *request_uri = NULL;
        char *request_method = NULL;
        char *query_string = NULL;
        grab_envs(envp, &remote_ip, &host, &request_uri, &request_method, &query_string);

        // Make sure this is a GET request
        if (strcasecmp(request_method, "GET") == 0 || strcasecmp(request_method, "POST") == 0)
        {
            // Check if this is a registration
            if (strcasecmp(host, REG_URL) == 0)
            {
                // This is a registration
                //test_envs();

                //char *url;
                //char *int_ip;
                //getParam("url", url);
                //getParam("local_ip", int_ip);

                //register_url(db, url, int_ip, remote_ip);

                // Get the query parameters
                char *new_reg_url = NULL;
                char *new_reg_ip = NULL;
                int new_reg_port;
                get_reg_params(query_string, &new_reg_url, &new_reg_ip, &new_reg_port);

                FCGX_FPrintF(out,"Content-type: text/plain\r\n\r\n");
                FCGX_FPrintF(out, "url: %s   ip: %s   port: %d", new_reg_url, new_reg_ip, new_reg_port);
            }
            else
            {
                //test_envs(out, envp);
                // Redirect to the new address
                char internal[16];
                char external[16];
                char *intPtr = (char *)&internal;
                char *extPtr = (char *)&external;
                int port;
                if (ips_for_host(db, host, &intPtr, &extPtr, &port))
                {
                    fprintf(stderr, "internal: %s   external: %s   port: %i\n", internal, external, port);

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
                    not_found(out);
                }
            }
        }
        else
        {
            // We can only handle GET requests
            unimplemented(out);
        }

        close_db(db);

        // For now, we're not doing anything with POST data
        //if (strcasecmp(request_method, "POST") == 0)
        //{
        //    char *postData = malloc(sizeof(char) * 10*1024*1024);
        //    FCGX_GetLine(postData, 10*1024*1024, in);
        //    if (postData != NULL)
        //    {
        //        FCGX_FPrintF(out, postData);
        //    }
        //    free(postData);
        //}
    }

    return 0;
}