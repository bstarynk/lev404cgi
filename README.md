lev404cgi
=========

Levenshtein like CGI handler for 404 Not found HTTP errors, proposing near paths.

Compile this single-file C++ program into a CGI.
his is a CGI program (for web servers) to deal with 404 (Not found)
errors using a modified Levenshtein distance to existing (static)
files on the web sever.

The program is a self-contained C++ file lev404.cc, whose compilation
command is near the end of the file.

Copyright 2013 Basile Starynkevitch



##### GPLv3 license notice:
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.



#### installation instructions:

The last dozen lines of the lev404.cc file gives compilation
instructions (e.g. for GNU/Linux/Debian); you should get a lev404.cgi file.

Then configure your web server to run that CGI for 404 Not-found errors. 
With Lighttpd, put lines like the following into your lighttpd.conf

$HTTP["host"] == "gcc-melt.org" {
   server.document-root = "/var/www/gcc-melt.org/"
   server.error-handler-404   = "/lev404.cgi"
   cgi.assign = ( ".cgi" => "" )
}

(the http://gcc-melt.org/ site is using this)
