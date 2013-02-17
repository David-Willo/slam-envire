#include "MLSGrid.hpp"
#include <fstream>
#include <limits>
#include <algorithm>

using namespace envire;

ENVIRONMENT_ITEM_DEF( MLSGrid )
/** For backward compatibility reasons, we have to export MLSGrid as
 * MultiLevelSurfaceGrid as well
 */
static SerializationPlugin<MLSGrid> factory("MultiLevelSurfaceGrid");

MLSGrid::MLSGrid()
    : GridBase()
    , cellcount( 0 )
{
    clear();
}

MLSGrid::MLSGrid(size_t cellSizeX, size_t cellSizeY, double scalex, double scaley, double offsetx, double offsety)
    : GridBase( cellSizeX, cellSizeY, scalex, scaley, offsetx, offsety )
    , cells( cellSizeX, cellSizeY )
    , cellcount( 0 )
{
    clear();
}

void MLSGrid::clear()
{
    cells.clear();
    cellcount = 0;
    if(index) index->reset();
    extents = Extents();
    config.useColor = false;
}

MLSGrid::MLSGrid(const MLSGrid& other)
    : GridBase( other )
    , cells( other.cells )
    , config( other.config )
    , cellcount( other.cellcount )
    , extents( other.extents )
{
}

MLSGrid& MLSGrid::operator=(const MLSGrid& other)
{
    if( this != &other )
    {
	GridBase::operator=(other);

	cells = other.cells;
	extents = other.extents;
	config = other.config;
	cellcount = other.cellcount;
    }

    return *this;
}

envire::MLSGrid* MLSGrid::cloneShallow() const
{
    MLSGrid* res = new MLSGrid( cellSizeX, cellSizeY, scalex, scaley, offsetx, offsety );
    res->config = config;
    return res;
}

MLSGrid::~MLSGrid()
{
}

void MLSGrid::serialize(Serialization& so)
{
    GridBase::serialize(so);

    so.write( "hasCellColor", config.useColor );
    writeMap( so.getBinaryOutputStream(getMapFileName() + ".mls") );
}

void MLSGrid::unserialize(Serialization& so)
{
    GridBase::unserialize(so);

    if( so.hasKey( "hasCellColor" ) )
	so.read( "hasCellColor", config.useColor );
    else
	config.useColor = false;

    cells.resize( cellSizeX, cellSizeY );

    // this is a workaround to make the MLS generatable by 
    // the GridBase::create method, which sets the map_count
    // to 0 to indicate that no map data needs to be loaded
    if( so.hasKey( "map_count" ) )
    {
	int map_count = 1;
	so.read( "map_count", map_count );
	if( map_count == 0 )
	    return;
    }

    std::istream *is;
    try 
    {
	is = &so.getBinaryInputStream(getMapFileName() + ".mls");
    }
    catch(Serialization::NoSuchBinaryStream)
    { 
	try
	{
	    is = &so.getBinaryInputStream(getMapFileName("envire::MultiLevelSurfaceGrid") + ".mls");
	}
	catch(Serialization::NoSuchBinaryStream)
	{
	    throw std::runtime_error("Could not get input stream for MLS.");
	}
    }
    readMap( *is );
}

// memory structure of version 1.0
struct SurfacePatchStore10
{
    double mean;
    double stdev;
    double height;
    bool horizontal;
    size_t update_idx;

    size_t xi, yi;

    SurfacePatch toSurfacePatch()
    {
	SurfacePatch p( mean, stdev, height, horizontal ? SurfacePatch::HORIZONTAL : SurfacePatch::VERTICAL );
	p.update_idx = update_idx;
	return p;
    }
};

// memory structure of version 1.1
struct SurfacePatchStore11
{
    double mean;
    double stdev;
    double height;
    bool horizontal;
    size_t update_idx;
    base::Vector3d color;

    size_t xi, yi;

    SurfacePatch toSurfacePatch()
    {
	SurfacePatch p( mean, stdev, height, horizontal ? SurfacePatch::HORIZONTAL : SurfacePatch::VERTICAL );
	p.update_idx = update_idx;
	p.setColor( color );
	return p;
    }
};

struct SurfacePatchStore12
{
    float mean;
    float stdev;
    float height;
    bool horizontal;
    size_t update_idx;
    uint8_t color[3];

    size_t xi, yi;

    SurfacePatch toSurfacePatch()
    {
	SurfacePatch p( mean, stdev, height, horizontal ? SurfacePatch::HORIZONTAL : SurfacePatch::VERTICAL );
	p.update_idx = update_idx;
	std::copy( color, color+3, p.color );
	return p;
    }
};

struct SurfacePatchStore : SurfacePatch
{
    SurfacePatchStore() {};

    SurfacePatchStore( const SurfacePatch& data, size_t xi, size_t yi )
	: SurfacePatch(data), xi(xi), yi(yi) {}

    size_t xi, yi;
};

void MLSGrid::writeMap(std::ostream& os)
{
    os << "mls" << std::endl;
    os << "1.2" << std::endl;
    os << sizeof( SurfacePatchStore ) << std::endl;
    os << "bin" << std::endl;

    for(size_t xi=0;xi<cellSizeX;xi++)
    {
	for(size_t yi=0;yi<cellSizeY;yi++)
	{
	    for( iterator it = beginCell( xi,yi ); it != endCell(); it++ )
	    {
		SurfacePatchStore d( *it, xi, yi );
		os.write( reinterpret_cast<const char*>(&d), sizeof( SurfacePatchStore ) );
	    }
	}
    }
}

void MLSGrid::readMap(std::istream& is)
{   
    char c[32];
    is.getline(c, 20);
    if( std::string(c) != "mls" )
	throw std::runtime_error("bad magic " + std::string(c));

    is.getline(c, 20);
    std::string version = std::string(c);
    if( version != "1.0" && version != "1.1" && version != "1.2" )
	throw std::runtime_error("version not supported " + version );

    is.getline(c, 20);
    if( version == "1.0" )
    {
	if( boost::lexical_cast<int>(std::string(c)) != sizeof( SurfacePatchStore10 ) )
	    throw std::runtime_error("binary size mismatch");
    }
    else if( version == "1.1" )
    {
	if( boost::lexical_cast<int>(std::string(c)) != sizeof( SurfacePatchStore11 ) )
	    throw std::runtime_error("binary size mismatch");
    }
    else if( version == "1.2" )
    {
	if( boost::lexical_cast<int>(std::string(c)) != sizeof( SurfacePatchStore12 ) )
	    throw std::runtime_error("binary size mismatch");
    }

    is.getline(c, 20);
    if( std::string(c) != "bin" )
	throw std::runtime_error("missing bin identifier" + std::string(c));

    if( version == "1.0" )
    {
	SurfacePatchStore10 d;
	while( is.read(reinterpret_cast<char*>(&d), sizeof( SurfacePatchStore10 ) ) )
	{
	    insertTail( d.xi, d.yi, d.toSurfacePatch() );
	}
    }
    else if( version == "1.1" )
    {
	SurfacePatchStore11 d;
	while( is.read(reinterpret_cast<char*>(&d), sizeof( SurfacePatchStore11 ) ) )
	{
	    insertTail( d.xi, d.yi, d.toSurfacePatch() );
	}
    }
    else if( version == "1.2" )
    {
	SurfacePatchStore12 d;
	while( is.read(reinterpret_cast<char*>(&d), sizeof( SurfacePatchStore12 ) ) )
	{
	    insertTail( d.xi, d.yi, d.toSurfacePatch() );
	}
    }
}

MLSGrid::iterator MLSGrid::beginCell( size_t xi, size_t yi )
{
    return cells.beginCell( xi, yi );
}

MLSGrid::const_iterator MLSGrid::beginCell( size_t xi, size_t yi ) const
{
    return cells.beginCell( xi, yi );
}

MLSGrid::iterator MLSGrid::endCell()
{
    return cells.endCell();
}

MLSGrid::const_iterator MLSGrid::endCell() const
{
    return cells.endCell();
}

void MLSGrid::insertHead( size_t xi, size_t yi, const SurfacePatch& value )
{
    cells.insertHead( xi, yi, value );
    addCell( Position( xi, yi ) );
}

void MLSGrid::insertTail( size_t xi, size_t yi, const SurfacePatch& value )
{
    cells.insertTail( xi, yi, value );
    addCell( Position( xi, yi ) );
}

MLSGrid::iterator MLSGrid::erase( iterator position )
{
    iterator res = cells.erase( position );
    cellcount--;
    return res; 
}

SurfacePatch* MLSGrid::get( const Position& position, const SurfacePatch& patch, double sigma_threshold, bool ignore_negative )
{
    MLSGrid::iterator it = beginCell(position.x, position.y);
    while( it != endCell() )
    {
	SurfacePatch &p(*it);
	const double interval = sqrt(sq(patch.stdev) + sq(p.stdev)) * sigma_threshold;
	if( p.distance( patch ) < interval && (!ignore_negative || !p.isNegative()) )
	{
	    return &p;
	}
	it++;
    }
    return NULL;
}


bool MLSGrid::get(const Eigen::Vector3d& position, double& zpos, double& zstdev )
{
    size_t xi, yi;
    if( toGrid(position.x(), position.y(), xi, yi) )
    {
	SurfacePatch patch( position.z(), zstdev ); 
	SurfacePatch *p = get( Position(xi, yi), patch );
	if( p )
	{
	    zpos = p->mean;
	    zstdev = p->stdev;
	    return true;
	}
    }
    return false;
}

void MLSGrid::updateCell( size_t xi, size_t yi, double mean, double stdev )
{
    updateCell( xi, yi, SurfacePatch( mean, stdev ) );
}

void MLSGrid::updateCell( const Position& pos, const SurfacePatch& o )
{
    updateCell( pos.x, pos.y, o );
}

void MLSGrid::updateCell( size_t xi, size_t yi, const SurfacePatch& co )
{
    typedef std::list<MLSGrid::iterator> iterator_list;
    iterator_list merged;
    // make a copy of the surfacepatch as it may get updated in the merge
    SurfacePatch o( co );

    for(MLSGrid::iterator it = beginCell( xi, yi ); it != endCell(); it++ )
    {
	// merge the patches and remember the ones which where merged 
	if( mergePatch( *it, o ) )
	    merged.push_back( it );
    }

    if( merged.empty() )
    {
	// insert the patch since we didn't merge it with any other
	insertHead( xi, yi, o );
    }
    else
    {
	// if there is more than one affected patch, merge them until 
	// there is only one left
	while( !merged.empty() )
	{
	    iterator_list::iterator it = ++merged.begin();
	    while( it != merged.end() ) 
	    {
		if( mergePatch( **merged.begin(), **it ) )
		{
		    erase( *it );
		    it = merged.erase( it );
		}
		else
		    it++;
	    }
	    merged.pop_front();
	}
    }
}

bool MLSGrid::mergePatch( SurfacePatch& p, SurfacePatch& o )
{
    return p.merge( o, config.gapSize, config.thickness, config.updateModel );
}

std::pair<SurfacePatch*, double> 
    getNearestPatch( const SurfacePatch& p, MLSGrid::iterator begin, MLSGrid::iterator end )
{
    SurfacePatch* min = NULL;
    double dist = std::numeric_limits<double>::infinity();

    // find the cell with the smallest z-diff
    while( begin != end )
    {
	double d;
	if( (d = p.distance( *begin )) < dist )
	{
	    min = &(*begin);
	    dist = d;
	}
	begin++;
    }

    return std::make_pair( min, dist );
}

void MLSGrid::merge( const MLSGrid& other, const Eigen::Affine3d& other2this, const SurfacePatch& offset )
{
    if( !other.getIndex() )
	throw std::runtime_error("MLSGrid::merge() currently only indexed sources are supported.");

    // need to handle cell color here for the update
    // we need to set the pgrid cell color, such that it matches
    // that of the scanmap for the update. Afterwards, we set it 
    // to its original value, if it previously had one.
    // if the other has cell color, also use it in the target grid
    bool hadCellColor = config.useColor;
    config.useColor = other.config.useColor;

    const std::set<Position> &cells = other.getIndex()->cells;

    // go through the index and merge each cell  
    for(std::set<Position>::iterator it = cells.begin(); it != cells.end(); it++)
    {
	// get center of cell and transform position
	// to this grid
	Eigen::Vector3d pos( Eigen::Vector3d::Zero() );
	other.fromGrid( it->x, it->y, pos.x(), pos.y() );
	pos = other2this * pos;

	// if it is still valid in this grid get cell position
	size_t m, n;
	if( toGrid( pos.x(), pos.y(), m, n ) )
	{
	    Position pos(m, n);
	    // iterate through cells in source map
	    for(envire::MLSGrid::const_iterator cit = other.beginCell(it->x,it->y); cit != other.endCell(); cit++ )
	    {
		SurfacePatch meas_patch( *cit );
		meas_patch.mean += offset.mean;
		meas_patch.stdev = sqrt( pow( meas_patch.stdev, 2 ) + pow( offset.stdev, 2 ) );
		meas_patch.update_idx = offset.update_idx;

		updateCell( pos.x, pos.y, meas_patch );
	    }
	}
    }

    if( hadCellColor )
	config.useColor = hadCellColor;
}

float MLSGrid::match( const MLSGrid& other, const Eigen::Affine3d& other2this, const SurfacePatch& offset, size_t sampling, float sigma )
{
    if( !other.getIndex() )
	throw std::runtime_error("MLSGrid::merge() currently only indexed sources are supported.");
    const std::set<Position> &cells = other.getIndex()->cells;

    // go through the index and match each cell  
    size_t idx = 0;
    size_t count = 0;
    size_t match = 0;
    for(std::set<Position>::iterator it = cells.begin(); it != cells.end(); it++)
    {
	if( idx++ % sampling == 0 )
	{
	    // get center of cell and transform position
	    // to this grid
	    Eigen::Vector3d pos( Eigen::Vector3d::Zero() );
	    other.fromGrid( it->x, it->y, pos.x(), pos.y() );
	    pos = other2this * pos;

	    // if it is still valid in this grid get cell position
	    size_t m, n;
	    if( toGrid( pos.x(), pos.y(), m, n ) )
	    {
		Position pos(m, n);
		// iterate through cells in source map
		envire::MLSGrid::const_iterator cit = other.beginCell(it->x,it->y); 
		if( cit != other.endCell() )
		{
		    while( cit != other.endCell() )
		    {
			SurfacePatch meas_patch( *cit );
			meas_patch.mean += offset.mean;
			meas_patch.stdev = sqrt( pow( meas_patch.stdev, 2 ) + pow( offset.stdev, 2 ) );
			meas_patch.update_idx = offset.update_idx;

			if( get( pos, meas_patch, sigma ) )
			{
			    ++match;
			    break;
			}
			++cit;
		    }
		    ++count;
		}
	    }
	}
    }

    if( count )
	return (float)match / (float)count;
    else
	return 1.0;
}

std::pair<double, double> MLSGrid::matchHeight( const MLSGrid& other )
{
    assert( other.getWidth() == getWidth() && other.getHeight() == getHeight() );

    double d1=0, d2=0;

    for(size_t xi=0;xi<cellSizeX;xi++)
    {
	for(size_t yi=0;yi<cellSizeY;yi++)
	{
	    if( other.beginCell(xi, yi) != other.endCell() && beginCell(xi, yi) != endCell() )
	    {
		for( const_iterator it = other.beginCell(xi,yi); it != other.endCell(); it++ )
		{
		    const SurfacePatch &p( *it );
		    std::pair<SurfacePatch*,double> res = getNearestPatch( p, beginCell(xi,yi), endCell() );

		    const double diff = res.second;
		    const double var = sq( res.first->stdev ) + sq( p.stdev );

		    d1 += diff / var;
		    d2 += 1.0 / var;
		}
	    }
	}
    }

    double delta = d1 / d2;
    double var = 1.0 / d2;

    return std::make_pair( delta, var );
}

void MLSGrid::addCell( const Position& pos )
{
    cellcount++;

    if( index )
	index->addCell(pos);

    Point2D p;
    fromGrid( pos, p );
    extents.extend( p );
}

void MLSGrid::initIndex()
{
   index = boost::shared_ptr<Index>( new Index() ); 
}
