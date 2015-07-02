/** file lev404.cc

    This file is a self contained C++ CGI program (for GNU/Linux
    x86-64) which deals with not found requests using a modified
    Levenshtein distance to existing (static) files on the web sever,
    and presenting to the browser some near-miss URLs.

    ****
    Copyright (C) 2013 - 2015 Basile Starynkevitch

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
#include <unistd.h>
#include <sys/time.h>
#include <dirent.h>
#include <ctype.h>

#include <math.h>
#include "cgicc/CgiDefs.h"
#include "cgicc/Cgicc.h"
#include "cgicc/HTTPStatusHeader.h"
#include "cgicc/HTTPHTMLHeader.h"

#ifdef GIT_COMMIT
#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)
#define GIT_COMMIT_STRING STRINGIFY(GIT_COMMIT)
#endif /*GIT_COMMIT*/

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
  // we favor compare of strings starting with the same four characters
  if (s1.size() > 10 && s2.size() > 10 && s1[0] == s2[0] && s1[1] == s2[1] && s1[2] == s2[2] && s1[3] == s2[3])
    tmp = tmp * 0.75;
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

#define LEV404_LOG_FILE "/var/log/lev404cgi.log"

void scan_directory(const std::string& dirstr, const std::string& reqpath, Pathvec_t& pathvec)
{
  struct dirent * ent = NULL;
  DIR* cdir = NULL;
  cdir = opendir(dirstr.c_str());
  if (cdir) {
    for (ent = readdir(cdir); ent != NULL; ent = readdir(cdir))
      {
	// skip hidden files, notably . & ..
	if (ent->d_name[0] == '.') 
	  continue;
	// skip short files, or backup files ending with % or ~ or CGI files
	int entlen = strlen(ent->d_name);
	if (entlen <= 5
	    || !isalnum(ent->d_name[0]) 
	    || !isalnum(ent->d_name[entlen-1])
	    || !strcmp(ent->d_name + entlen-3, "cgi")
	    )
	  continue;
	{
	  Path p;
	  
	  if (dirstr == "." || dirstr == "./" || dirstr.empty())
	    {
	      double s = edit_distance(ent->d_name, reqpath);
	      p.score = s;
	      p.name = ent->d_name;
	    }
	  else
	    {
	      string fnam = dirstr;
	      if (dirstr[dirstr.size()-1] != '/') fnam += "/";
	      fnam += ent->d_name;
	      p.score = edit_distance(fnam, reqpath);
	      p.name = fnam;
	    }
	  pathvec.push_back (p);
	}
      }
    closedir(cdir);
  }
  else
    syslog(LOG_NOTICE, "failed to scan directory %s - %m", dirstr.c_str());
} // end of scan_directory


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
  char pwdbuf[256];
  memset (pwdbuf, 0, sizeof(pwdbuf));
  if (!getcwd(pwdbuf, sizeof(pwdbuf)-1))
    syslog (LOG_WARNING, "getcwd failed - %m");
  pwdbuf[sizeof(pwdbuf)-1] = '\0';
  // Create a new Cgicc object containing all the CGI data
  Cgicc cgi;
  const CgiEnvironment& env = cgi.getEnvironment();
  string reqpath = getenv("REQUEST_URI")?:env.getPathInfo();
  string origreqpath = reqpath;
  // log this run, in addition of what the web server might do
  syslog (LOG_NOTICE, "reqpath=%s method=%s at gmtime=%s remotehost=%s remoteip=%s %s",
	  reqpath.c_str(), env.getRequestMethod().c_str(), nowbuf, 
	  env.getRemoteHost().c_str(), env.getRemoteAddr().c_str(),
#ifdef GIT_COMMIT
	  "gitcomm=" GIT_COMMIT_STRING
#else
	  __DATE__ "@" __TIME__
#endif
	  );
  { 
    FILE *flog = fopen(LEV404_LOG_FILE, "a");
    if (flog != NULL) {
      if (ftell(flog)==0) {
	fprintf(flog, "## log file %s; from "
		"https://github.com/bstarynk/lev404cgi\n", LEV404_LOG_FILE);
	fprintf(flog, "##<time> <reqpath> <reqmethod> <remotehost> <remoteaddr> (<gitcommit>)\n");
      }
      fprintf(flog, "%s\t%s\t%s\t%s\t%s",
	      nowbuf, reqpath.c_str(), env.getRequestMethod().c_str(), 
	      env.getRemoteHost().c_str(), env.getRemoteAddr().c_str());
#ifdef GIT_COMMIT
      fprintf(flog, "\t(%s)", GIT_COMMIT_STRING);
#else
      fprintf(flog, "\t[%s]",  __DATE__ "@" __TIME__);
#endif 
      fputc('\n', flog);
      fclose (flog);
    }
    else 
      syslog (LOG_WARNING, "could not open %s - %m", LEV404_LOG_FILE);
  }
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
  cout << "<!-- Unix cwd: " << pwdbuf << " -->" << endl;
  cout << "<!-- HTTP reqpath: " << reqpath << " -->" << endl;
  cout << "<body><h1>404 - Not found</h1>" << endl;
  cout << "<p><tt>" << env.getRequestMethod() << "</tt> of path <tt>" <<
    reqpath << "</tt> at " << nowbuf << ".</p>" << endl;  
  if (!reqpath.empty() && reqpath[0] == '/') 
    reqpath.erase(0,1);
  Pathvec_t pathvec;
  string reqdir = reqpath.substr(0, reqpath.find ('/'));
  /// look into the request topmost directory
  scan_directory(reqdir, reqpath, pathvec);
  cout << "<!-- reqdir=" << reqdir << " nbdirpaths=" << pathvec.size() << "; reqpath=" << reqpath << " -->" << endl;
  /// look into the current, i.e. webdocumentroot, directory
  scan_directory(".", reqpath, pathvec);
  sort(pathvec.begin(), pathvec.end());
  cout << "<!-- #paths: " << pathvec.size() << " -->" << endl;
  if (!pathvec.empty()) {
    cout << "<p>Possible suggestions (with similarity score): <ul>" << endl;
    int cnt = 0;
#ifndef NDEBUG
    {
#define LEVL404DBG "/tmp/levl404dbg"
      FILE *pfil = fopen(LEVL404DBG, "w");
      if (pfil) {
	syslog (LOG_NOTICE, "reqpath=%s debug output in %s", reqpath.c_str(), LEVL404DBG);
	fprintf(pfil, "**nowbuf %s; pwdbuf %s; reqdir %s\n", nowbuf, pwdbuf, reqdir.c_str());
	fprintf(pfil, "**reqpath is %s\n", reqpath.c_str());
	for (Pathvec_t::iterator it= pathvec.begin(); it != pathvec.end(); it++)
	  {
	    fprintf(pfil, "name=%s score=%g\n", it->name.c_str(), it->score);
	  }
	fclose(pfil);
      }
    }
#warning with debug output to /tmp/levl404dbg
#endif /*NDEBUG*/
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
  cout << "<p><small>Generated with <a href='https://github.com/bstarynk/lev404cgi'>lev404cgi</a>" << endl;
#ifdef GIT_COMMIT
  //#pragma message "git-commit:" STRINGIFY(GIT_COMMIT)
  cout << " git-commit <tt>" GIT_COMMIT_STRING "</tt>";
#endif /*GIT_COMMIT*/
  cout << ".</small></p>" << endl;
  cout << "</body></html>" << endl;
  return EXIT_SUCCESS;
}
  
    

/**** for emacs
  ++ Local Variables: ++
  ++ compile-command: "g++ -O -Wall -g -DGIT_COMMIT=`git log -n 1 --abbrev=16 --format=%h` lev404.cc /usr/lib/libcgicc.a  -lm -o lev404.cgi" ++
  ++ End: ++

  To install, configure your web server appropriately
 ****/
