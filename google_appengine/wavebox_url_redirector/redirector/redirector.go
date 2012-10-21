package redirector

import (
	"fmt"
	"net/http"
	"appengine"
	"appengine/datastore"
	"time"
	"strconv"
	"strings"
)

type HostRecord struct {
	Host 		string
	RemoteIp    string
	LocalIp 	string
	Port 		int64
	IsSecure	bool
	ServerId 	string
	Timestamp 	int64
}

type ErrorCode int32
const (
	ErrorCode_DatabaseError 	= iota
	ErrorCode_MissingParameter
	ErrorCode_AddressInUse
)

func errorMessage(c ErrorCode) (string) {
	switch c {
	case ErrorCode_DatabaseError:
		return "Database Error"
	case ErrorCode_MissingParameter:
		return "Missing Parameter"
	case ErrorCode_AddressInUse:
		return "Address in use"
	}
	return ""
}

/*
 * URL Registration
 */

// Get a host record from datastore
func getUrlRecord(host string, r *http.Request) (*HostRecord, error) {
	c := appengine.NewContext(r)
	k := datastore.NewKey(c, "Entity", host, 0, nil)
	e := new(HostRecord)

	if err := datastore.Get(c, k, e); err != nil {
		return e, err
    }
    return e, nil
}

// Put a new host url record into datastore
func registerUrl(record *HostRecord, r *http.Request) (error) {
	c := appengine.NewContext(r)
	k := datastore.NewKey(c, "Entity", record.Host, 0, nil)

	_, err := datastore.Put(c, k, record)
	return err
}

/*
 * Header writing
 */

// Perform a 302 redirect to the user's address
func tempRedirect(w http.ResponseWriter, r *http.Request, location string, port int64, isSecure bool) {
	var l string
	if isSecure { 
		l = "https" 
	} else { 
		l = "http" 
	}
	l = fmt.Sprintf("%s://%s:%d", l, location, port)

	header := w.Header()	
	header.Set("Content-Length", "0")
	header.Set("Location", l)
	header.Set("Connection", "close")
	w.WriteHeader(302)
}

// Send the not implemented header, used for request types other 
// than GET and POST
func unimplemented(w http.ResponseWriter, r *http.Request) {
	header := w.Header()
	header.Set("Allow", "GET, POST")
	header.Set("Content-type", "text/html")
	w.WriteHeader(501)
}

// Send the not found header, used for unregistered URLs
func notFound(w http.ResponseWriter, r *http.Request) {
	header := w.Header()
    header.Set("Content-type", "text/html")
    w.WriteHeader(404)
}

// Send the registration success message
func regSuccess(w http.ResponseWriter, r *http.Request) {
	header := w.Header()
	header.Set("Content-type", "application/json")
	fmt.Fprint(w, "{\"success\":true}")
}

// Send the registration failure message
func regFailure(w http.ResponseWriter, r *http.Request, c ErrorCode) {
	header := w.Header()
    header.Set("Content-type", "application/json")
    fmt.Fprintf(w, "{\"success\":false,\"errorCode\":%d,\"errorMessage\":\"%s\"}", c, errorMessage(c)) 
}

/*
 * Request Handling
 */

func init() {
	http.HandleFunc("/", handler)
}

func handleRegistration(w http.ResponseWriter, r *http.Request) {

	// Parse the query parameters
	r.ParseForm()
	host := r.Form.Get("host")
	serverId := r.Form.Get("serverId")
	port := r.Form.Get("port")
	isSecure := r.Form.Get("isSecure")
	remoteIp := r.RemoteAddr
	localIp := r.Form.Get("localIp")

	// Make sure all parameters exist
	if len(host) == 0 || len(serverId) == 0 || len(port) == 0 || len(isSecure) == 0 || len(remoteIp) == 0 || len(localIp) == 0 {
		regFailure(w, r, ErrorCode_MissingParameter)

	// Check if this registration exists, and bail on error (unless the error is from the record not existing)
	} else if record, err := getUrlRecord(host, r); err != nil && err != datastore.ErrNoSuchEntity {
		regFailure(w, r, ErrorCode_DatabaseError)

	} else {

		// Check if the registration either doesn't yet exist, is older than 30 days, or this is am update from the same server
		if err == datastore.ErrNoSuchEntity || record.Timestamp <= time.Now().Unix() - (60*60*24*30) || serverId == record.ServerId { 

			// Make sure port and secure parse properly
			port, err := strconv.ParseInt(port, 10, 32)
			if err != nil {
				regFailure(w, r, ErrorCode_MissingParameter)
			}
			secure, err := strconv.ParseBool(isSecure)
			if err != nil {
				regFailure(w, r, ErrorCode_MissingParameter)
			}

			// Create a new host record for this registration
			record := HostRecord {
				Host: host,
				RemoteIp: remoteIp,
				LocalIp: localIp,
				Port: port,
				IsSecure: secure,
				ServerId: serverId,
				Timestamp: time.Now().Unix(),
			}

			// Register the user
			if err = registerUrl(&record, r); err != nil {
				regFailure(w, r, ErrorCode_DatabaseError)
			} else {
				regSuccess(w, r)
			}

		} else {
			// Inform the user the address is in use
			regFailure(w, r, ErrorCode_AddressInUse)
		}
	} 
}

func handleRedirect(w http.ResponseWriter, r *http.Request) {

	// Check if the host is registered yet
	if record, err := getUrlRecord(r.Host, r); err != nil {

		// No one has registered this yet, send a 404
		notFound(w, r)
	} else {
		
		// If they are connecting from inside their network
		// redirect to the internal IP, otherwise use the external
		var l string
		if r.RemoteAddr == record.RemoteIp {
			l = record.LocalIp
		} else {
			l = record.RemoteIp
		}

		tempRedirect(w, r, l, record.Port, record.IsSecure)
	}	
}

func handler(w http.ResponseWriter, r *http.Request) {
	// Make sure this is a GET or POST request
	if r.Method == "GET" || r.Method == "POST" {
		// Check if this is a registration
		if strings.Contains(r.Host, "register.") {
			handleRegistration(w, r)
		} else {
			handleRedirect(w, r)
		}
	} else {
		unimplemented(w, r)
	}
}