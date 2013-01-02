/** file lev404.cc

    This file is a self contained C++ CGI program (for GNU/Linux
    x86-64) which deals with not found requests using a modified
    Levenshtein distance to existing (static) files on the web sever,
    and presenting to the browser some near-miss URLs.

    ****
    Copyright (C) 2013 Basile Starynkevitch

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

    ****
See
http://en.wikibooks.org/wiki/Algorithm_implementation/Strings/Levenshtein_distance
http://en.wikipedia.org/wiki/Damerau%E2%80%93Levenshtein_distance
**/

#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <syslog.h>
#include <time.h>
#include <sys/time.h>
#include <dirent.h>
#include <ctype.h>

#include <math.h>
#include "cgicc/CgiDefs.h"
#include "cgicc/Cgicc.h"
#include "cgicc/HTTPStatusHeader.h"
#include "cgicc/HTTPHTMLHeader.h"

using namespace std;
using namespace cgicc;

extern "C" char**environ;

const double cost_del = 1.1;
const double cost_ins = 1.5;
const double cost_sub = 1.8;

double edit_distance( const std::string& s1, const std::string& s2 )
{
  unsigned int n1 = s1.length();
  unsigned int n2 = s2.length();

   double* p = new double[n2+1];
   double* q = new double[n2+1];
   double* r = NULL;

  p[0] = 0;
  for( unsigned int j = 1; j <= n2; ++j )
    p[j] = p[j-1] + cost_ins;

  for( unsigned int i = 1; i <= n1; ++i )
    {
      q[0] = p[0] + cost_del;
      for( unsigned int j = 1; j <= n2; ++j )
        {
          double d_del = p[j] + cost_del;
          double d_ins = q[j-1] + cost_ins;
          double d_sub = p[j-1] + ( s1[i-1] == s2[j-1] ? 0 : (int)(cost_sub*cbrt(abs(s1[i-1]-s2[i-1])/sqrt(i+j+3))) );
          q[j] = std::min( std::min( d_del, d_ins ), d_sub );
      }
      r = p;
      p = q;
      q = r;
    }

  double tmp = p[n2];
  delete[] p;
  delete[] q;

  return tmp;
}

struct Path {
  std::string name;
  double score;
};


bool operator < (const Path& p1, const Path& p2)
{
  return p1.score < p2.score;
}

typedef vector<Path> Pathvec_t;

// Main Street, USA
int
main(int /*argc*/, 
     char ** /*argv*/)
{
  openlog ("lev404.cgi",LOG_PID,LOG_USER);
  timeval start={0,0};
  gettimeofday(&start, NULL);  
  time_t now = start.tv_sec;
  char nowbuf[64];
  memset (nowbuf, 0, sizeof(nowbuf));
  strftime(nowbuf, sizeof(nowbuf)-1, 
	   "%Y-%b-%d %H:%M:%S %Z",
	   gmtime(&now));
  // Create a new Cgicc object containing all the CGI data
  Cgicc cgi;
  const CgiEnvironment& env = cgi.getEnvironment();
  string reqpath = getenv("REQUEST_URI")?:env.getPathInfo();
  cout << "Status: 404 Not Found" << "\r\n";
  cout << "Content-Type: text/html" << "\r\n";
  cout << "\r\n" << endl;
  cout << "<!DOCTYPE html>" << endl;
  cout << "<head><title>404 - Not Found</title></head>" << endl;
  cout << "<!-- see https://github.com/bstarynk/lev404cgi -->" << endl;
  cout << "<!-- " 
       << __FILE__ << " built " << __DATE__ "@" __TIME__ << " -->" << endl;
  cout << "<!-- CGI environment: " << endl;
#define ShowEnv(X) cout << "  " << #X << "=" << env.X() << endl
  ShowEnv(getServerSoftware);
  ShowEnv(getServerName);
  ShowEnv(getGatewayInterface);
  ShowEnv(getServerProtocol);
  ShowEnv(getServerPort);
  ShowEnv(getCookies);
  ShowEnv(getRequestMethod);
  ShowEnv(getPathInfo);
  ShowEnv(getPathTranslated);
  ShowEnv(getScriptName);
  ShowEnv(getQueryString);
  ShowEnv(getUserAgent);
  cout << " -->" << endl;
  cout << "<!-- Unix environment: " << endl;
  for (char**e = environ; e && *e; e++)
    cout << " " << *e << endl;
  cout << " -->" << endl;
  cout << "<body><h1>404 - Not found</h1>" << endl;
  cout << "<p><tt>" << env.getRequestMethod() << "</tt> of path <tt>" <<
    reqpath << "</tt> at " << nowbuf << ".</p>" << endl;
  
  if (!reqpath.empty() && reqpath[0] == '/') 
    reqpath.erase(0,1);
  Pathvec_t pathvec;
  struct dirent * ent = NULL;
  DIR* cdir = opendir(".");
  if (cdir) {
    for (ent = readdir(cdir); ent != NULL; ent = readdir(cdir))
      {
	// skip hidden files, notably . & ..
	if (ent->d_name[0] == '.') 
	  continue;
	// skip short files, or backup files ending with % or ~ or CGI files
	int entlen = strlen(ent->d_name);
	if (entlen <= 4
	    || !isalnum(ent->d_name[0]) 
	    || !isalnum(ent->d_name[entlen-1])
	    || !strcmp(ent->d_name + entlen-3, "cgi")
	    )
	  continue;
	{
	  Path p;
	  p.name = ent->d_name;
	  p.score = edit_distance(p.name, reqpath);
	  pathvec.push_back (p);
	}
      }
    closedir(cdir);
  }

  sort(pathvec.begin(), pathvec.end());

  cout << "<!-- #paths: " << pathvec.size() << " -->" << endl;
  if (!pathvec.empty()) {
    cout << "<p>Possible suggestions (with similarity score): <ul>" << endl;
    int cnt = 0;
    for (Pathvec_t::iterator it= pathvec.begin(); it != pathvec.end(); it++)
      {
	cnt++;
	if (cnt>12) break;
	cout << "<li><a href='" << it->name << "'>" << it->name << "</a>"
	     << " <small>(" << it->score << ")</small></li>" << endl;
      }
    if (cnt < (int)pathvec.size()) 
      cout << " etc ....";
    cout << "</ul></p>" << endl;
  }

  cout << "<hr/>" << endl;
  cout << "<small>Generated with <a href='https://github.com/bstarynk/lev404cgi'>lev404cgi</a></small>" << endl;
  cout << "</body></html>" << endl;
  return EXIT_SUCCESS;
}
  
    

/**** for emacs
  ++ Local Variables: ++
  ++ compile-command: "g++ -O -Wall -g lev404.cc /usr/lib/libcgicc.a  -lm -o lev404.cgi" ++
  ++ End: ++

  To install, configure your web server appropriately
 ****/