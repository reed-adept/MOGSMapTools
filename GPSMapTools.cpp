/*
Adept MobileRobots Advanced Robotics Navigation and Localization (ARNL)

Copyright (C) 2004-2005 ActivMedia Robotics LLC
Copyright (C) 2006-2009 MobileRobots Inc.
Copyright (C) 2010-2016 Adept Technology, Inc.

All Rights Reserved.

Adept MobileRobots does not make any representations about the
suitability of this software for any purpose.  It is provided "as is"
without express or implied warranty.

The license for this software is distributed as LICENSE.txt in the top
level directory.

robots@mobilerobots.com
Adept MobileRobots
10 Columbia Drive
Amherst, NH 03031
+1-603-881-7960

*/


#include "GPSMapTools.h"
#include <assert.h>

AREXPORT GPSMapTools::GPSMapTools(ArGPS *gps, ArRobot* robot, ArServerHandlerCommands *commandServer, ArMap *map, ArServerHandlerMap *ms, ArServerHandlerPopup *popupServer) :
  myGPS(gps),
  myMap(map),
  myRobot(robot),
  myPopupServer(popupServer),
  myLastGoalNum(0),
  myHaveMapGPSCoords(false),
  myForbiddenLineStarted(false),
  myObstacleLineStarted(false),
  myStartNewMapCB(this, &GPSMapTools::startNewMap),
  myAddGoalHereCB(this, &GPSMapTools::addGoalHere),
  myAddHomeHereCB(this, &GPSMapTools::addHomeHere),
  mySetOriginCB(this, &GPSMapTools::setOrigin),
  myStartForbiddenLineCB(this, &GPSMapTools::startForbiddenLine),
  myEndForbiddenLineCB(this, &GPSMapTools::endForbiddenLine),
  myStartObstacleLineCB(this, &GPSMapTools::startObstacleLine),
  myEndObstacleLineCB(this, &GPSMapTools::endObstacleLine),
  myReloadMapFileCB(this, &GPSMapTools::reloadMapFile),
  myResetRobotPoseCB(this, &GPSMapTools::resetRobotPose),
  myMapServer(ms)
{
  commandServer->addStringCommand("Map:startNewMap", "Start a new map using current GPS position as origin. Provide map name (required)", &myStartNewMapCB);
  commandServer->addStringCommand("Map:addGoalHere", "Modify the map to include a goal at the current GPS location. Provide goal name.", &myAddGoalHereCB);
  commandServer->addStringCommand("Map:addHomeHere", "Modify the map to include a home at the current GPS location.", &myAddHomeHereCB);
  commandServer->addCommand("Map:setMapOriginHere", "Set the georeference point for map center (origin) at the current GPS position to the map. Only use this command if the robot is at the map center point (origin of map). Use this to add a georeference point to a map generated by laser scanning and processing.", &mySetOriginCB);
  commandServer->addCommand("Map:forbiddenLineStart", "Begin a new forbidden line", &myStartForbiddenLineCB);
  commandServer->addCommand("Map:forbiddenLineEnd", "End a forbidden line that was started with forbiddenLineStart", &myEndForbiddenLineCB);
//  commandServer->addCommand("Map:obstacleLineStart", "Begin a new obstacle line", &myStartObstacleLineCB);
//  commandServer->addCommand("Map:obstacleLineEnd", "End a obstacle line that was started with obstacleLineStart", &myEndObstacleLineCB);
  commandServer->addCommand("Map:reloadFile", "Reload map from its file on the robot", &myReloadMapFileCB);
  commandServer->addCommand("ResetRobotPose", "Reset robot pose to 0,0,0", &myResetRobotPoseCB);
}

bool GPSMapTools::checkGPS(const std::string &action)
{
  if(!myGPS) {
    ArLog::log(ArLog::Terse, "GPSMapTools: error %s: NULL GPS object!", action.c_str());
    return false;
  }
  if(!myGPS->havePosition()) {
    ArLog::log(ArLog::Terse, "GPSMapTools: error %s: GPS doesn't have a valid position.", action.c_str());
    return false;
  }
  ArLog::log(ArLog::Normal, "GPSMapTools: %s at GPS position (lat=%f, lon=%f, alt=%f)  Robot pos is (x=%.2f, y=%.2f). HDOP is %f. Map origin is (%f, %f, %f)", 
    action.c_str(), myGPS->getLatitude(), myGPS->getLongitude(), myGPS->getAltitude(), 
    myRobot->getX(), myRobot->getY(),
    myGPS->haveHDOP() ? myGPS->getHDOP() : 0.0,
    myMap->getOriginLatLong().getX(), myMap->getOriginLatLong().getY(), myMap->getOriginAltitude()
  );
  return true;
}

bool GPSMapTools::checkMap(const std::string& action)
{
  if(!myMap) {
    ArLog::log(ArLog::Terse, "GPSMapTools: error %s: NULL map", action.c_str());
    return false;
  }
  if(strlen(myMap->getFileName()) == 0) {
    ArLog::log(ArLog::Terse, "GPSMapTools: error %s: no map file name", action.c_str());
    return false;
  }
  return true;
}

void GPSMapTools::startNewMap(ArArgumentBuilder *args)
{
  std::string filename = args->getFullString();
  if(filename.rfind(".map") == filename.npos)
    filename += ".map"; 
  ArLog::log(ArLog::Normal, "GPSMapTools: starting new map with name \"%s\".", filename.c_str());
  if(!checkGPS("starting new map")) return;
  myMap->lock();
  myMap->clear();
  myMap->writeFile(filename.c_str(), true);
  //myMap->unlock();
  //myMap->lock();
  myMap->readFileAndChangeConfig(filename.c_str());
	// TODO config not changing, need to force write of config?
  myMap->unlock();
  setOrigin();
  // TODO place boundary forbidden lines, or add a command to setup boundaries
  // based on desired size of map.
}

void GPSMapTools::resetMapCoords(const ArLLACoords& mapOrigin)
{
  myMapGPSCoords = ArMapGPSCoords(mapOrigin);
  myHaveMapGPSCoords = true;
}

void GPSMapTools::resetRobotPose()
{
  if(myRobot)
  {
    ArLog::log(ArLog::Normal, "GPSMapTools: Resetting robot odometric position to 0,0,0.");
    myRobot->moveTo(0,0,0);
    myRobot->com(ArCommands::SETO);
    myRobot->com(ArCommands::SIM_RESET);
  }
  else
  {
    ArLog::log(ArLog::Normal, "GPSMapTools: Can't reset robot pose, have no robot pointer!");
  }
}

void GPSMapTools::setOrigin()
{
  if(!checkGPS("setting map origin")) return;
  if(!checkMap("setting map origin")) return;
  resetRobotPose();
  if(myMap->hasOriginLatLongAlt())
  {
    ArLog::log(ArLog::Terse, "GPSMapTools: setting map origin: Warning: Map already has an origin point, it will be replaced by current position.");
  }
  myOrigin = ArLLACoords(myGPS->getLatitude(), myGPS->getLongitude(), myGPS->getAltitude());
  resetMapCoords(myOrigin);
  myMap->lock();
  myMap->setOriginLatLongAlt(true, ArPose(myGPS->getLatitude(), myGPS->getLongitude()), myGPS->getAltitude());
  myMap->writeFile(myMap->getFileName(), true);
  myMap->unlock();
  reloadMapFile();
}

ArPose GPSMapTools::getCurrentPosFromGPS()
{
  ArLLACoords gpsCoords(myGPS->getLatitude(), myGPS->getLongitude(), myGPS->getAltitude());
  double x, y, z;
  x = y = z = NAN;
  // XXX need to have previously saved origin in myMapGPSCoords; either recreate
  // here each time, or reset whenever a new map is either created or loaded
  // elsewhere, and in ctor if there is a map in arnl.
  if(!myHaveMapGPSCoords)
  {
    // get origin from ArMap and setup MAp->GPS coords translator
    if(myMap->hasOriginLatLongAlt())
    {
      ArPose p = myMap->getOriginLatLong();
      myOrigin = ArLLACoords(p.getX(), p.getY(), myMap->getOriginAltitude());
      resetMapCoords(myOrigin);
    }
  }
  
  myMapGPSCoords.convertLLA2MapCoords(gpsCoords, x, y, z);
  return ArPose(x, y);
}
  

void GPSMapTools::addGoalHere(ArArgumentBuilder *args)
{
  if(!checkGPS("adding goal")) return;
  if(!checkMap("adding goal")) return;
  ArPose p = getCurrentPosFromGPS();
  ArLog::log(ArLog::Normal, "GPSMapTools: Adding goal %s in map at GPS position (x=%.2f, y=%.2f)", args->getFullString(), p.getX(), p.getY());
  myMap->lock();
  ArMapObject *newobj;
  myMap->getMapObjects()->push_back(newobj = new ArMapObject("Goal", p, args->getFullString(), "ICON", args->getFullString(), false, ArPose(0, 0), ArPose(0, 0)));
  printf("\tnew map object is:\n\t%s\n", newobj->toString());
  newobj->log();
  myMap->writeFile(myMap->getFileName(), true);
  myMap->unlock();
  reloadMapFile();
}

void GPSMapTools::addHomeHere(ArArgumentBuilder *args)
{
  if(!checkGPS("adding home")) return;
  if(!checkMap("adding home")) return;
  ArPose p = getCurrentPosFromGPS();
  ArLog::log(ArLog::Normal, "GPSMapTools: Adding home in map at GPS position (x=%.2f, y=%.2f)",  p.getX(), p.getY());
  myMap->lock();
  ArMapObject *newobj;
  myMap->getMapObjects()->push_back(newobj = new ArMapObject("Home", p, "Home", "ICON", args->getFullString(), false, ArPose(0, 0), ArPose(0, 0)));
  printf("\tnew map object is:\n\t%s\n", newobj->toString());
  newobj->log();
  myMap->writeFile(myMap->getFileName(), true);
  myMap->unlock();
  reloadMapFile();
}

void GPSMapTools::startForbiddenLine()
{
  if(!checkGPS("starting forbidden line")) return;
  if(!checkMap("starting forbidden line")) return;
  if(myForbiddenLineStarted)
    endForbiddenLine();
  myForbiddenLineStart = getCurrentPosFromGPS();
  ArLog::log(ArLog::Normal, "GPSMapTools: Started forbidden line at %f, %f.", myForbiddenLineStart.getX(), myForbiddenLineStart.getY());
  myForbiddenLineStarted = true;
}

void GPSMapTools::endForbiddenLine()
{
  if(!myForbiddenLineStarted) return;
  if(!checkGPS("ending forbidden line")) return;
  if(!checkMap("ending forbidden line")) return;
  ArPose endpos = getCurrentPosFromGPS();
  ArLog::log(ArLog::Normal, "GPSMapTools: Ended forbidden line at %f, %f", endpos.getX(), endpos.getY());
  ArMapObject* obj = new ArMapObject("ForbiddenLine", myForbiddenLineStart, "", "ICON", "", true, myForbiddenLineStart, endpos);
  myMap->lock();
  myMap->getMapObjects()->push_back(obj);
  myMap->writeFile(myMap->getFileName(), true);
  myMap->unlock();
  myForbiddenLineStarted = false;
  reloadMapFile();
}

void GPSMapTools::startObstacleLine()
{
  // TODO split into placing obstacle line on left or right side of robot
  if(!checkGPS("starting obstacle line")) return;
  if(!checkMap("starting obstacle line")) return;
  if(myObstacleLineStarted)
    endObstacleLine();
  myObstacleLineStart = getCurrentPosFromGPS();
  ArLog::log(ArLog::Normal, "GPSMapTools: Started obstacle line at %f, %f.", myObstacleLineStart.getX(), myObstacleLineStart.getY());
  myObstacleLineStarted = true;
}

void GPSMapTools::endObstacleLine()
{
  // TODO split into placing obstacle line on left or right side of robot
  if(!myObstacleLineStarted) return;
  if(!checkGPS("ending obstacle line")) return;
  if(!checkMap("ending obstacle line")) return;
  ArPose endpos = getCurrentPosFromGPS();
  ArLog::log(ArLog::Normal, "GPSMapTools: Ended obstacle line at %f, %f", endpos.getX(), endpos.getY());
  ArLineSegment line(myObstacleLineStart, endpos);
  myMap->lock();
  std::vector<ArLineSegment>* lines = myMap->getLines();
  lines->push_back(line);
  myMap->setLines(lines);
  myMap->writeFile(myMap->getFileName(), true);
  myMap->unlock();
  myObstacleLineStarted = false;
  reloadMapFile();
}

void GPSMapTools::reloadMapFile()
{
  myMap->refresh();
  myMap->readFile(myMap->getFileName());
  //myMap->mapChanged(); // should cause ArServerHandlerMap to send new map and goals list
  //if(myMapServer)
  //  myMapServer->mapChanged();
}
