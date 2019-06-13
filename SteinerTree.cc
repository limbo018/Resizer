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

#include <fstream>
#include <string>
#include "Machine.hh"
#include "Report.hh"
#include "Error.hh"
#include "Debug.hh"
#include "LefDefNetwork.hh"
#include "SteinerTree.hh"

namespace sta {

using std::string;

static bool
fileExists(const string &filename);
static void
connectedPins(const Net *net,
	      Network *network,
	      // Return value.
	      PinSeq &pins);

bool
readFluteInits(string dir)
{
  string flute_path1 = dir;
  string flute_path2 = dir;
  flute_path1 += "/etc/POWV9.dat";
  flute_path2 += "/etc/PORT9.dat";
  if (fileExists(flute_path1) && fileExists(flute_path2)) {
    readLUT(flute_path1.c_str(), flute_path2.c_str());
    return true;
  }
  else
    return false;
}

// c++17 std::filesystem::exists
static bool
fileExists(const string &filename)
{
  std::ifstream stream(filename.c_str());
  return stream.good();
}

////////////////////////////////////////////////////////////////

SteinerPt SteinerTree::null_pt = -1;

SteinerTree *
makeSteinerTree(const Net *net,
		bool find_left_rights,
		LefDefNetwork *network)
{
  Network *sdc_network = network->sdcNetwork();
  Debug *debug = network->debug();
  Report *report = network->report();
  debugPrint1(debug, "steiner", 1, "Net %s\n",
	      sdc_network->pathName(net));

  SteinerTree *tree = new SteinerTree();
  PinSeq &pins = tree->pins();
  connectedPins(net, network, pins);
  int pin_count = pins.size();
  if (pin_count >= 2) {
    FluteDbu x[pin_count];
    FluteDbu y[pin_count];
    // map[pin_index] -> steiner tree vertex index
    int pin_map[pin_count];

    for (int i = 0; i < pin_count; i++) {
      Pin *pin = pins[i];
      DefPt loc = network->location(pin);
      x[i] = loc.x();
      y[i] = loc.y();
      debugPrint3(debug, "steiner", 3, "%s (%d %d)\n",
		  sdc_network->pathName(pin),
		  loc.x(), loc.y());
    }

    int flute_accuracy = 3;
    Tree stree = flute(pin_count, x, y, flute_accuracy, pin_map);
    tree->setTree(stree, pin_map);
    if (debug->check("steiner", 3)) {
      printtree(stree);
      report->print("pin map\n");
      for (int i = 0; i < pin_count; i++)
	report->print(" %d -> %d\n", i, pin_map[i]);
    }
    if (find_left_rights)
      tree->findLeftRights(network);
    if (debug->check("steiner", 2))
      tree->report(sdc_network);
    return tree;
  }
  else
    return nullptr;
}

static void
connectedPins(const Net *net,
	      Network *network,
	      // Return value.
	      PinSeq &pins)
{
  NetConnectedPinIterator *pin_iter = network->connectedPinIterator(net);
  while (pin_iter->hasNext()) {
    Pin *pin = pin_iter->next();
    pins.push_back(pin);
  }
  delete pin_iter;
}

void
SteinerTree::setTree(Tree tree,
		     int pin_map[])
{
  tree_ = tree;

  // Invert the steiner vertex to pin index map.
  int pin_count = pinCount();
  pin_map_.resize(pin_count);
  for (int i = 0; i < pin_count; i++)
    pin_map_[pin_map[i]] = i;
  findSteinerPtAliases();
}

bool
SteinerTree::isPlaced(LefDefNetwork *network) const
{
  for (auto pin : pins_) {
    if (network->isPlaced(pin))
      return true;
  }
  return false;
}

int
SteinerTree::branchCount() const
{
  return tree_.deg * 2 - 2;
}

void
SteinerTree::branch(int index,
		    // Return values.
		    DefPt &pt1,
		    Pin *&pin1,
		    int &steiner_pt1,
		    DefPt &pt2,
		    Pin *&pin2,
		    int &steiner_pt2,
		    int &wire_length)
{
  Branch &branch_pt1 = tree_.branch[index];
  int index2 = branch_pt1.n;
  Branch &branch_pt2 = tree_.branch[index2];
  pt1 = DefPt(branch_pt1.x, branch_pt1.y);
  if (index < pinCount()) {
    pin1 = pin(index);
    steiner_pt1 = 0;
  }
  else {
    pin1 = nullptr;
    steiner_pt1 = index;
  }

  pt2 = DefPt(branch_pt2.x, branch_pt2.y);
  if (index2 < pinCount()) {
    pin2 = pin(index2);
    steiner_pt2 = 0;
  }
  else {
    pin2 = nullptr;
    steiner_pt2 = index2;
  }

  wire_length = abs(branch_pt1.x - branch_pt2.x)
    + abs(branch_pt1.y - branch_pt2.y);
}

void
SteinerTree::report(const Network *network)
{
  Report *report = network->report();
  int branch_count = branchCount();
  for (int i = 0; i < branch_count; i++) {
    Branch &pt1 = tree_.branch[i];
    int j = pt1.n;
    Branch &pt2 = tree_.branch[j];
    int wire_length = abs(pt1.x - pt2.x) + abs(pt1.y - pt2.y);
    report->print(" %s (%d %d) - %s wire_length = %d",
		  name(i, network),
		  static_cast<int>(pt1.x),
		  static_cast<int>(pt1.y),
		  name(j, network),
		  wire_length);
    if (left_.size()) {
      SteinerPt left = this->left(i);
      SteinerPt right = this->right(i);
      report->print(" left = %s right = %s",
		    name(left, network),
		    name(right, network));
    }
    report->print("\n");
  }
}

void
SteinerTree::findSteinerPtAliases()
{
  int pin_count = pinCount();
  for(int i = 0; i < pin_count; i++) {
    Branch &branch_pt1 = tree_.branch[i];
    steiner_pt_pin_alias_map_[DefPt(branch_pt1.x, branch_pt1.y)] = pins_[pin_map_[i]];
  }
}

Pin *
SteinerTree::steinerPtAlias(SteinerPt pt)
{
  Branch &branch_pt = tree_.branch[pt];
  return steiner_pt_pin_alias_map_[DefPt(branch_pt.x, branch_pt.y)];
}

const char *
SteinerTree::name(SteinerPt pt,
		  const Network *network)
{
  if (pt == SteinerTree::null_pt)
    return "NULL";
  else {
    checkSteinerPt(pt);
    const Pin *pin = this->pin(pt);
    if (pin)
      return network->pathName(pin);
    else
      return stringPrintTmp("S%d", pt);
  }
}

Pin *
SteinerTree::pin(SteinerPt pt) const
{
  checkSteinerPt(pt);
  if (pt < pinCount())
    return pins_[pin_map_[pt]];
  else
    return nullptr;
}

SteinerPt
SteinerTree::drvrPt(const Network *network) const
{
  int pin_count = pinCount();
  for (int i = 0; i < pin_count; i++) {
    Pin *pin = this->pin(i);
    if (network->isDriver(pin))
      return i;
  }
  return SteinerTree::null_pt;
}

void
SteinerTree::checkSteinerPt(SteinerPt pt) const
{
  if (pt < 0 || pt >= branchCount())
    internalError("steiner point index out of range.");
}

bool
SteinerTree::isLoad(SteinerPt pt,
		    const Network *network)
{
  checkSteinerPt(pt);
  Pin *pin = this->pin(pt);
  return pin && network->isLoad(pin);
}

DefPt
SteinerTree::location(SteinerPt pt) const
{
  checkSteinerPt(pt);
  Branch &branch_pt = tree_.branch[pt];
  return DefPt(branch_pt.x, branch_pt.y);
}

void
SteinerTree::findLeftRights(const Network *network)
{
  Debug *debug = network->debug();
  int branch_count = branchCount();
  left_.resize(branch_count, SteinerTree::null_pt);
  right_.resize(branch_count, SteinerTree::null_pt);
  int pin_count = pinCount();
  SteinerPtSeq adj1(branch_count, SteinerTree::null_pt);
  SteinerPtSeq adj2(branch_count, SteinerTree::null_pt);
  SteinerPtSeq adj3(branch_count, SteinerTree::null_pt);
  for (int i = 0; i < branch_count; i++) {
    Branch &branch_pt = tree_.branch[i];
    SteinerPt j = branch_pt.n;
    if (j != i) {
      if (adj1[i] == SteinerTree::null_pt)
	adj1[i] = j;
      else if (adj2[i] == SteinerTree::null_pt)
	adj2[i] = j;
      else
	adj3[i] = j;

      if (adj1[j] == SteinerTree::null_pt)
	adj1[j] = i;
      else if (adj2[j] == SteinerTree::null_pt)
	adj2[j] = i;
      else
	adj3[j] = i;
    }
  }
  if (debug->check("steiner", 3)) {
    printf("adjacent\n");
    for (int i = 0; i < branch_count; i++) {
      printf("%d:", i);
      if (adj1[i] != SteinerTree::null_pt)
	printf(" %d", adj1[i]);
      if (adj2[i] != SteinerTree::null_pt)
	printf(" %d", adj2[i]);
      if (adj3[i] != SteinerTree::null_pt)
	printf(" %d", adj3[i]);
      printf("\n");
    }
  }
  SteinerPt root = drvrPt(network);
  SteinerPt root_adj = adj1[root];
  left_[root] = root_adj;
  findLeftRights(root, root_adj, adj1, adj2, adj3);
}

void
SteinerTree::findLeftRights(SteinerPt from,
			    SteinerPt to,
			    SteinerPtSeq &adj1,
			    SteinerPtSeq &adj2,
			    SteinerPtSeq &adj3)
{
  if (to >= pinCount()) {
    SteinerPt adj;
    adj = adj1[to];
    findLeftRights(from, to, adj, adj1, adj2, adj3);
    adj = adj2[to];
    findLeftRights(from, to, adj, adj1, adj2, adj3);
    adj = adj3[to];
    findLeftRights(from, to, adj, adj1, adj2, adj3);
  }
}

void
SteinerTree::findLeftRights(SteinerPt from,
			    SteinerPt to,
			    SteinerPt adj,
			    SteinerPtSeq &adj1,
			    SteinerPtSeq &adj2,
			    SteinerPtSeq &adj3)
{
  if (adj != from && adj != SteinerTree::null_pt) {
    if (adj == to)
      internalError("steiner left/right failed");
    if (left_[to] == SteinerTree::null_pt) {
      left_[to] = adj;
      findLeftRights(to, adj, adj1, adj2, adj3);
    }
    else if (right_[to] == SteinerTree::null_pt) {
      right_[to] = adj;
      findLeftRights(to, adj, adj1, adj2, adj3);
    }
  }
}

SteinerPt
SteinerTree::left(SteinerPt pt)
{
  return left_[pt];
}

SteinerPt
SteinerTree::right(SteinerPt pt)
{
  return right_[pt];
}

};
