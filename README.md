WaveBoxUrlRedirector
====================

A FastCGI plugin written in C for redirecting urls to user machines. BSD Licensed.

Requires FastCGI and Sqlite3 to be installed. No other dependencies.

To run, use spawn-fcgi like so:

spawn-fcgi -a 127.0.0.1 -p 6000 -n ./fastcgi_redirector

Then configure Nginx or Apache to forward all requests.

On Amazon EC2 Linux I had to specify the lib director for fcgi like so:

LD_LIBRARY_PATH=/usr/local/lib spawn-fcgi -a 127.0.0.1 -p 6000 -n ./fastcgi_redirector