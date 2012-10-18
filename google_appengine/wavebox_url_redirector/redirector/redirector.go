package redirector

import (
	"fmt"
	"net/http"
	"appengine"
	"appengine/datastore"
	"time"
	"strconv"
)

type HostRecord struct {
	Host 		string
	RemoteIp    string
	LocalIp 	string
	Port 		uint64
	IsSecure	bool
	ServerId 	string
	Timestamp 	int64
}

func init() {
	http.HandleFunc("/", handler)
}

func handler(w http.ResponseWriter, r *http.Request) {
	
	// Grab environment
	remoteIp := r.RemoteAddr
	host := r.Host
	//path := r.URL.Path
	method := r.Method
	r.ParseForm()
	query := r.Form

	//fmt.Fprintf(w, "remoteIp: %v  host: %v  path: %v  method: %v  query: %v", remoteIp, host, path, method, query)

	// Make sure this is a GET or POST request
	if method == "GET" || method == "POST" {

		// Check if this is a registration
		if host == "register.benjamm.in" {

			if record, err := getUrlRecord(host, r); err != nil && err != datastore.ErrNoSuchEntity {

				regFailure(w, r, "Database error")

			} else {

				// Check if registration older than 30 days
				if err == datastore.ErrNoSuchEntity || record.Timestamp <= time.Now().Unix() - (60*60*24*30) || query["serverId"][0] == record.ServerId { 

					port, err := strconv.ParseUint(query["port"][0], 10, 32)
					secure, err := strconv.ParseBool(query["isSecure"][0])

					// Register the user
					record := HostRecord {
						Host: query["host"][0],
						RemoteIp: query["remoteIp"][0],
						LocalIp: query["localIp"][0],
						Port: port,
						IsSecure: secure,
						ServerId: query["serverId"][0],
						Timestamp: time.Now().Unix(),
					}

					if err = registerUrl(record, r); err != nil {
						regFailure(w, r, "Database error")
					} else {
						regSuccess(w, r)
					}

				} else {
					regFailure(w, r, "Address already taken")
				}
			} 
		} else {

			if record, err := getUrlRecord(host, r); err != nil {
				notFound(w, r)
			} else {

				var l string
				if remoteIp == record.RemoteIp {
					l = record.RemoteIp
				} else {
					l = record.LocalIp
				}

				tempRedirect(w, r, l, strconv.FormatUint(record.Port, 10), record.IsSecure)
			}	

		}

	} else {
		unimplemented(w, r)
	}
}

/*
 * URL Registration
 */

func getUrlRecord(host string, r *http.Request) (HostRecord, error) {
	c := appengine.NewContext(r)
	k := datastore.NewKey(c, "Entity", host, 0, nil)
	e := new(HostRecord)

	if err := datastore.Get(c, k, e); err != nil {
		return *e, err
    }
    return *e, nil
}

func registerUrl(record HostRecord, r *http.Request) (error) {
	c := appengine.NewContext(r)
	k := datastore.NewKey(c, "Entity", record.Host, 0, nil)

	_, err := datastore.Put(c, k, record)
	return err
}

/*
 * Header writing
 */

// Perform a 302 redirect to the user's address
func tempRedirect(w http.ResponseWriter, r *http.Request, location string, port string, isSecure bool) {
	var l string
	if isSecure { 
		l = "https" 
	} else { 
		l = "http" 
	}
	l = fmt.Sprint("%s://%s:%d", l, location, port)

	header := w.Header()	
	header.Set("Status", "302 Moved Temporarily")
	header.Set("Content-Length", "0")
	header.Set("Location", l)
	header.Set("Connection", "close")
}

// Send the not implemented header, used for request types other 
// than GET and POST
func unimplemented(w http.ResponseWriter, r *http.Request) {
	header := w.Header()
	header.Set("Status", "501 Method Not Implemented")
	header.Set("Allow", "GET, POST")
	header.Set("Content-type", "text/html")
}

// Send the not found header, used for unregistered URLs
func notFound(w http.ResponseWriter, r *http.Request) {
	header := w.Header()
    header.Set("Status", "404 Not Found")
    header.Set("Content-type", "text/html")	
}

// Send the registration success message
func regSuccess(w http.ResponseWriter, r *http.Request) {
	header := w.Header()
    header.Set("Status", "200 OK")
	header.Set("Content-type", "application/json")
	fmt.Fprint(w, "{\"success\":true\r\n}")
}

// Send the registration failure message
func regFailure(w http.ResponseWriter, r *http.Request, errorMsg string) {
	header := w.Header()
    header.Set("Status", "200 OK")
    header.Set("Content-type", "application/json")
    fmt.Fprintf(w, "{\"success\":false,\"error\":\"%s\"", errorMsg) 
}
