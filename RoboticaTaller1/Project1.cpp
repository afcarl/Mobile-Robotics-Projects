#include "Aria.h"
#include<cmath>
#define PI acos(-1.0)
#define EPS 1e-6
class PrintingTask
{
public:
	// Constructor. Adds our 'user task' to the given robot object.
	PrintingTask(ArRobot *robot);

	// Destructor. Removes our user task from the robot
	~PrintingTask(void);

	// This method will be called by the callback functor
	void doTask(void);
protected:
	ArRobot *myRobot;

	// The functor to add to the robot for our 'user task'.
	ArFunctorC<PrintingTask> myTaskCB;
};


// the constructor (note how it uses chaining to initialize myTaskCB)
PrintingTask::PrintingTask(ArRobot *robot) :
myTaskCB(this, &PrintingTask::doTask)
{
	myRobot = robot;
	// just add it to the robot
	myRobot->addSensorInterpTask("PrintingTask", 50, &myTaskCB);
}
FILE * outputFile;
PrintingTask::~PrintingTask()
{
	myRobot->remSensorInterpTask(&myTaskCB);
}

//precision to be used, by default we use double
typedef double prec;
//Struct for using vectors in 2D, comes with addition
struct Point2D {
	prec x, y;
	Point2D(const prec &_x, const prec &_y) {
		x = _x;
		y = _y;
	}
	Point2D() {
		x = 0;
		y = 0;
	}
	Point2D operator+ (const Point2D &e1) const {
		return Point2D(x + e1.x, y + e1.y);
	}
	bool operator== (const Point2D &e1) const {
		return (abs(x - e1.x) < EPS) && (abs(y - e1.y) < EPS);
	}
};
//Struct for using integer points, this will avoid repeating points
struct IntegerPoint2D {
	int x, y;
	IntegerPoint2D(const int &_x, const int &_y) {
		x = _x;
		y = _y;
	}
	IntegerPoint2D() {
		x = 0;
		y = 0;
	}
	IntegerPoint2D operator+ (const IntegerPoint2D &e1) const {
		return IntegerPoint2D(x + e1.x, y + e1.y);
	}
	bool operator< (const IntegerPoint2D &e1) const {
		if (x == e1.x) return y < e1.y;
		return x < e1.x;
	}
};
std::set<IntegerPoint2D> knownPoints;
//struct for representing the transformation
struct Transformation {
	Point2D translation;
	prec rotation;
	prec matrix[2][2];
	//_translation is a Point2D with the translation vector
	//_rotation represents the angle (in radians, for deg, multiply by PI/180)
	Transformation(const Point2D& _translation, const prec& _rotation) {
		translation = Point2D(_translation.x, _translation.y);
		rotation = _rotation;
		matrix[0][0] = cos(rotation);
		matrix[0][1] = -1 * sin(rotation);
		matrix[1][0] = sin(rotation);
		matrix[1][1] = cos(rotation);
	}
	Transformation() {
		translation = Point2D(0, 0);
		matrix[0][0] = matrix[1][1] = 1;
		matrix[0][1] = matrix[1][0] = 0;
	}
	//Applies the transformation to a point, returning another point.
	Point2D apply(const Point2D& input) {
		Point2D res = Point2D(input.x*matrix[0][0] + input.y*matrix[0][1], input.x*matrix[1][0] + input.y*matrix[1][1]);
		res = res + translation;
		return res;
	}
	bool operator== (const Transformation &e1) const {
		return (e1.translation == translation) && (abs(matrix[0][0] - e1.matrix[0][0]) < EPS && abs(matrix[0][1] - e1.matrix[0][1]) < EPS);
	}
}lastKnownLocation;
void PrintingTask::doTask(void)
{
	// print out some info about the robot
	myRobot->lock();

	/*ArLog::log(ArLog::Normal, "Robot Coordinates: \n\tx %6.1f    y %6.1f    th %6.1f    vel %7.1f    mpacs %3d \n\t", myRobot->getX(),
		myRobot->getY(), myRobot->getTh(), myRobot->getVel(),
		myRobot->getMotorPacCount());*/
	Transformation currentPos = Transformation(Point2D(myRobot->getX(), myRobot->getY()), myRobot->getTh()*PI / 180);
	bool hasChanged = !(currentPos == lastKnownLocation);
	lastKnownLocation = currentPos;
	myRobot->unlock();



	int numLasers = 0;

	// Get a pointer to ArRobot's list of connected lasers. We will lock the robot while using it to prevent changes by tasks in the robot's background task thread or any other threads. Each laser has an index. You can also store the laser's index or name (laser->getName()) and use that to get a reference (pointer) to the laser object using ArRobot::findLaser().
	myRobot->lock();
	std::map<int, ArLaser*> *lasers = myRobot->getLaserMap();

	//ArLog::log(ArLog::Normal, "ArRobot provided a set of %d ArLaser objects.", lasers->size());

	for (std::map<int, ArLaser*>::const_iterator i = lasers->begin(); i != lasers->end(); ++i)
	{
		int laserIndex = (*i).first;
		ArLaser* laser = (*i).second;
		if (!laser)
			continue;
		++numLasers;
		laser->lockDevice();

		// The current readings are a set of obstacle readings (with X,Y positions as well as other attributes) that are the most recent set from teh laser.
		std::list<ArPoseWithTime*> *currentReadings = laser->getCurrentBuffer(); // see ArRangeDevice interface doc
		for (std::list<ArPoseWithTime*>::iterator it = currentReadings->begin(); it != currentReadings->end() && laser->isConnected() && hasChanged; it++) {
			IntegerPoint2D point = IntegerPoint2D((int)floor((*it)->getX()), (int)floor((*it)->getY()));
			if (knownPoints.find(point) == knownPoints.end()) {
				knownPoints.insert(point);
				fprintf(outputFile, "%d\t%d\n", point.x, point.y);
			}
		}
		// The raw readings are just range or other data supplied by the sensor. It may also include some device-specific extra values associated with each reading as well. (e.g. Reflectance for LMS200)
//		const std::list<ArSensorReading*> *rawReadings = laser->getRawReadings();

	/*	ArLog::log(ArLog::Normal, "Laser #%d (%s): %s.\n\tHave %d 'current' readings.\n\t", //Have %d 'raw' readings.\n\t",
			laserIndex, laser->getName(), (laser->isConnected() ? "connected" : "NOT CONNECTED"),
			currentReadings->size());*/
//			rawReadings->size())
		laser->unlockDevice();
	}
	if (numLasers == 0)
		ArLog::log(ArLog::Normal, "No lasers.");
	else
		ArLog::log(ArLog::Normal, "");
	ArLog::log(ArLog::Normal, "Points Detected: %d", knownPoints.size());
	// Unlock robot and sleep for 5 seconds before next loop.
	myRobot->unlock();
	//ArUtil::sleep(5000);
}




	// Need sensor readings? Try myRobot->getRangeDevices() to get all 
	// range devices, then for each device in the list, call lockDevice(), 
	// getCurrentBuffer() to get a list of recent sensor reading positions, then
	// unlockDevice().


int main(int argc, char** argv)
{
	outputFile = fopen("data.map", "w");
	fprintf(outputFile, "2D-Map\nDATA\n");
	Aria::init();
	ArArgumentParser parser(&argc, argv);
	parser.loadDefaultArguments();
	ArRobot robot;

	ArRobotConnector robotConnector(&parser, &robot);
	ArLaserConnector laserConnector(&parser, &robot, &robotConnector);

	if (!robotConnector.connectRobot())
	{
		ArLog::log(ArLog::Terse, "robotSyncTaskExample: Could not connect to the robot.");
		if (parser.checkHelpAndWarnUnparsed())
		{
			// -help not given
			Aria::logOptions();
			Aria::exit(1);
		}
	}

	if (!Aria::parseArgs() || !parser.checkHelpAndWarnUnparsed())
	{
		Aria::logOptions();
		Aria::exit(1);
	}

	// Connect to laser(s) defined in parameter files, if LaseAutoConnect is true
	// for the laser. 
	// (Some flags are available as arguments to connectLasers() to control error behavior and to control which lasers are put in the list of lasers stored by ArRobot. See docs for details.)
	if (!laserConnector.connectLasers())
	{
		ArLog::log(ArLog::Terse, "Could not connect to configured lasers. Exiting.");
		Aria::exit(3);
		return 3;
	}

	ArLog::log(ArLog::Normal, "Connected to all lasers.");

	ArLog::log(ArLog::Normal, "robotSyncTaskExample: Connected to robot.");

	// This object encapsulates the task we want to do every cycle. 
	// Upon creation, it puts a callback functor in the ArRobot object
	// as a 'user task'.
	PrintingTask pt(&robot);

	// initialize aria
	Aria::init();

	// the keydrive action
	ArActionKeydrive keydriveAct;

	robot.enableMotors();
	robot.addAction(&keydriveAct, 45);


	// Start the robot process cycle running. Each cycle, it calls the robot's
	// tasks. When the PrintingTask was created above, it added a new
	// task to the robot. 'true' means that if the robot connection
	// is lost, then ArRobot's processing cycle ends and this call returns.
	robot.run(true);

	// Allow some time to read laser data
	ArUtil::sleep(500);

	printf("Disconnected. Goodbye.\n");

	return 0;
}
