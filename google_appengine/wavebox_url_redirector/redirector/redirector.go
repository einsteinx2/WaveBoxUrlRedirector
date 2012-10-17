package redirector

import (
	"fmt"
	"net/http"
)

func init() {
	http.HandleFunc("/", handler)
}

func handler(w http.ResponseWriter, r *http.Request) {
	
	// Grab environment
	//remoteIp := r.RemoteAddr
	host := r.Host
	//path := r.URL.Path
	method := r.Method
	//r.ParseForm()
	//query := r.Form

	//fmt.Fprintf(w, "remoteIp: %v  host: %v  path: %v  method: %v  query: %v", remoteIp, host, path, method, query)

	// Make sure this is a GET or POST request
	if (method == "GET" || method == "POST") {
		
		// Check if this is a registration
		if (host == "register.benjamm.in") {

		} else {
			tempRedirect(w, r, "http://home.benjamm.in:4040")
		}
	} else {
		unimplemented(w, r);
	}
}

// Perform a 302 redirect to the user's address
func tempRedirect(w http.ResponseWriter, r *http.Request, location string) {
	header := w.Header()	
	header.Set("Status", "302 Moved Temporarily")
	header.Set("Content-Length", "0")
	header.Set("Location", location)
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
	fmt.Fprint(w, "{\"success\":true\r\n}");
}

// Send the registration failure message
func regFailure(w http.ResponseWriter, r *http.Request, errorMsg string) {
	header := w.Header()
    header.Set("Status", "200 OK")
    header.Set("Content-type", "application/json")
    fmt.Fprintf(w, "{\"success\":false,\"error\":\"%s\"", errorMsg);
}
