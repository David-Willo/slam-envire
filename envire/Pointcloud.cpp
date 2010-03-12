#include "Core.hpp"
#include "Pointcloud.hpp"

#include <fstream>

using namespace envire;

const std::string Pointcloud::className = "envire::Pointcloud";

Pointcloud::Pointcloud()
{
}

Pointcloud::~Pointcloud()
{
}

Pointcloud::Pointcloud(Serialization& so)
    : CartesianMap(so)
{
    so.setClassName(className);

    readMap( so.getMapPath() + ".txt" );
}

void Pointcloud::serialize(Serialization& so)
{
    CartesianMap::serialize(so);
    so.setClassName(className);

    writeMap( so.getMapPath() + ".txt" );
}

Pointcloud* Pointcloud::clone() 
{
    return new Pointcloud(*this);
}

void Pointcloud::writeMap(const std::string& path)
{
    std::ofstream data(path.c_str());
    if( data.fail() )  
    {
        throw std::runtime_error("Could not open file '" + path + "' for writing.");
    }

    for(int i=0;i<vertices.size();i++)
    {
	data << vertices[i].x() << " " << vertices[i].y() << " " << vertices[i].y() << std::endl;
    }
}

void Pointcloud::readMap(const std::string& path)
{
    std::ifstream data(path.c_str());
    if( data.fail() )  
    {
        throw std::runtime_error("Could not open file '" + path + "'.");
    }

    while( !data.eof() )
    {
	double x, y, z;
	data >> x >> y >> z;
	vertices.push_back( Eigen::Vector3d( x,y,z ) );
    }
}

Pointcloud* Pointcloud::importCsv(const std::string& path, FrameNode* fm)
{
    Pointcloud* pc = new Pointcloud();
    pc->readMap( path );

    Environment* env = fm->getEnvironment();
    env->attachItem(pc);
    pc->setFrameNode(fm);

    return pc;
}

