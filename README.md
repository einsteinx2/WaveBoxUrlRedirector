WaveBoxUrlRedirector
====================

A simple URL redirection server for Google App Engine, written in Go. BSD Licensed. Used by WaveBox to provide dynamic URLs.

--------------------------------------------------------------------------------------------
wavebox_url_redirector.c (deprecated):

A FastCGI plugin written in C for redirecting urls to user machines. BSD Licensed.

Requires FastCGI and Sqlite3 to be installed. No other dependencies.

To run, use spawn-fcgi like so:

spawn-fcgi -a 127.0.0.1 -p 6000 -n ./wavebox_url_redirector

Then configure Nginx or Apache to forward all requests.

On Amazon EC2 Linux I had to specify the lib director for fcgi like so:

LD_LIBRARY_PATH=/usr/local/lib spawn-fcgi -a 127.0.0.1 -p 6000 -n ./wavebox_url_redirector