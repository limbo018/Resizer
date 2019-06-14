// Resizer, LEF/DEF gate resizer
// Copyright (c) 2019, Parallax Software, Inc.
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <stdio.h>
#include "Machine.hh"
#include "Error.hh"
#include "StringUtil.hh"
#include "LefDefNetwork.hh"
#include "defiComponent.hpp"
#include "defiNet.hpp"
#include "DefWriter.hh"

namespace sta {

static void
writeDefComponents(FILE *out_stream,
		   LefDefNetwork *network);
static void
writeDefComponent(Instance *inst,
		  FILE *out_stream,
		  LefDefNetwork *network);
static void
writeDefNets(FILE *out_stream,
	     LefDefNetwork *network);
static void
writeDefNet(Net *net,
	    FILE *out_stream,
	    LefDefNetwork *network);
static const char *
staToDef(const char *token,
	 Network *network);

////////////////////////////////////////////////////////////////

void
writeDef(const char *filename,
	 LefDefNetwork *network)
{
  const char *in_filename = network->filename();
  FILE *in_stream = fopen(in_filename, "r");
  if (in_stream) {
    FILE *out_stream = fopen(filename, "w");
    if (out_stream) {
      size_t buffer_size = 128;
      char *buffer = new char[buffer_size];
      while (getline(&buffer, &buffer_size, in_stream) >= 0) {
	
	if (stringBeginEqual(buffer, "COMPONENTS ")) {
	  // Skip the components.
	  do {
	    getline(&buffer, &buffer_size, in_stream);
	  } while (!stringBeginEqual(buffer, "END COMPONENTS")
		   && !feof(in_stream));
	  writeDefComponents(out_stream, network);
	}
	else if (stringBeginEqual(buffer, "NETS ")) {
	  // Skip the nets.
	  do {
	    getline(&buffer, &buffer_size, in_stream);
	  } while (!stringBeginEqual(buffer, "END NETS")
		   && !feof(in_stream));
	  writeDefNets(out_stream, network);
	}
	else
	  fputs(buffer, out_stream);
      }
      delete [] buffer;
      fclose(out_stream);
    }
    else
      throw FileNotWritable(filename);
    fclose(in_stream);
  }
  else
    throw FileNotReadable(in_filename);
}

static void
writeDefComponents(FILE *out_stream,
		   LefDefNetwork *network)
{
  fprintf(out_stream, "COMPONENTS %d ;\n",
	  network->leafInstanceCount());

  LeafInstanceIterator *leaf_iter = network->leafInstanceIterator();
  while (leaf_iter->hasNext()) {
    Instance *inst = leaf_iter->next();
    writeDefComponent(inst, out_stream, network);
  }
  delete leaf_iter;

  fprintf(out_stream, "END COMPONENTS\n\n");
}

static void
writeDefComponent(Instance *inst,
		  FILE *out_stream,
		  LefDefNetwork *network)
{
  defiComponent *def_component = network->defComponent(inst);
  fprintf(out_stream, "- %s %s",
	  staToDef(network->name(inst), network),
	  network->name(network->cell(inst)));
  if (def_component) {
    if (def_component->hasEEQ())
      fprintf(out_stream, "\n+ EEQMASTER %s ", def_component->EEQ());
    if (def_component->hasGenerate())
      fprintf(out_stream, "\n+ GENERATE %s %s",
	      def_component->generateName(),
	      def_component->macroName());
    if (def_component->hasSource())
      fprintf(out_stream, "\n+ SOURCE %s", def_component->source());
    if (def_component->hasForeignName())
      fprintf(out_stream, "\n+ FOREIGN %s ( %d %d ) %s",
	      def_component->foreignName(),
	      def_component->foreignX(),
	      def_component->foreignY(),
	      def_component->foreignOri());
    int status = def_component->placementStatus();
    if (status) {
      if (status == DEFI_COMPONENT_UNPLACED)
	fprintf(out_stream, "\n+ UNPLACED");
      else {
	const char *status_key;
	switch (status) {
	case DEFI_COMPONENT_PLACED:
	  status_key = "PLACED";
	  break;
	case DEFI_COMPONENT_FIXED:
	  status_key = "FIXED";
	  break;
	case DEFI_COMPONENT_COVER:
	  status_key = "COVER";
	  break;
	}
	fprintf(out_stream, "\n+ %s ( %d %d ) %s",
		status_key,
		def_component->placementX(),
		def_component->placementY(),
		def_component->placementOrientStr());
      }
    }
    if (def_component->hasWeight())
      fprintf(out_stream, "\n+ WEIGHT %d",
	      def_component->weight());
    if (def_component->hasRegionName())
      fprintf(out_stream, "\n+ REGION %s",
	      def_component->regionName());
    if (def_component->hasRegionBounds()) {
      int size, *xl, *yl, *xh, *yh;
      def_component->regionBounds(&size, &xl, &yl, &xh, &yh);
      fprintf(out_stream, "\n+ REGION ( %d %d ) ( %d %d )",
	      xl[0], yl[0], xh[0], yh[0]);
    }
  }
  fprintf(out_stream, " ;\n");
}
  
static void
writeDefNets(FILE *out_stream,
	     LefDefNetwork *network)
{
  fprintf(out_stream, "NETS %d ;\n",
	  network->netCount());
  
  NetIterator *net_iter = network->netIterator(network->topInstance());
  while (net_iter->hasNext()) {
    Net *net = net_iter->next();
    writeDefNet(net, out_stream, network);
  }
  delete net_iter;
  
  fprintf(out_stream, "END NETS\n\n");
}

static void
writeDefNet(Net *net,
	    FILE *out_stream,
	    LefDefNetwork *network)
{
  const char *sta_net_name = network->name(net);
  const char *def_net_name = staToDef(sta_net_name, network);
  fprintf(out_stream, "- %s", def_net_name);
  int column = strlen(def_net_name) + 2;
  int column_max = 80;

  NetTermIterator *term_iter = network->termIterator(net);
  while (term_iter->hasNext()) {
    Term *term = term_iter->next();
    const char *port_name = network->portName(term);
    fprintf(out_stream, " ( PIN %s )",
	    port_name);
    int width = strlen(port_name) + 9;
    if ((column + width) > column_max) {
      fprintf(out_stream, "\n ");
      column = 0;
    }
    column += width;
  }
  delete term_iter;

  NetPinIterator *pin_iter = network->pinIterator(net);
  while (pin_iter->hasNext()) {
    Pin *pin = pin_iter->next();
    const char *sta_component_name = network->name(network->instance(pin));
    const char *def_component_name = staToDef(sta_component_name, network);
    const char *port_name = network->portName(pin);
    fprintf(out_stream, " ( %s %s )",
	    def_component_name,
	    port_name);
    int width = strlen(def_component_name) + strlen(port_name) + 6;
    if ((column + width) > column_max) {
      fprintf(out_stream, "\n ");
      column = 0;
    }
    column += width;
  }
  delete pin_iter;
  fprintf(out_stream, " ;\n");
}

// Remove path divider escapes in token.
static const char *
staToDef(const char *token,
	 Network *network)
{
  char path_escape = network->pathEscape();
  char path_divider = network->pathDivider();
  char *unescaped = makeTmpString(strlen(token) + 1);
  char *u = unescaped;
  for (const char *s = token; *s ; s++) {
    char ch = *s;

    if (ch != path_escape)
      *u++ = ch;
  }
  *u = '\0';
  return unescaped;
}

} // namespace
