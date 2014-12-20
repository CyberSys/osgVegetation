#include "QuadTreeScattering.h"
#include <osg/AlphaFunc>
#include <osg/Billboard>
#include <osg/BlendFunc>
#include <osg/Depth>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Material>
#include <osg/Math>
#include <osg/MatrixTransform>
#include <osg/ShapeDrawable>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/TextureBuffer>
#include <osg/Image>
#include <osg/TexEnv>
#include <osg/ComputeBoundsVisitor>
#include <osg/PagedLOD>
#include <osg/ProxyNode>


#include <osgDB/WriteFile>
#include <osgDB/ReadFile>
#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>
#include <osg/Texture2DArray>
//#include <iostream>
#include <sstream>
#include "BRTGeometryShader.h"
#include "BRTShaderInstancing.h"
#include "VegetationUtils.h"
#include "ITerrainQuery.h"

namespace osgVegetation
{
	QuadTreeScattering::QuadTreeScattering(osg::Node* terrain, ITerrainQuery* tq) : m_Terrain(terrain),
		m_VRT(NULL),
		m_TerrainQuery(tq),
		m_UsePagedLOD(false),
		m_FilenamePrefix("quadtree_"),
		m_DensityLODRatio(0.5)
	{
		
	}

	void QuadTreeScattering::_populateVegetationLayer(const BillboardLayer& layer,const  osg::BoundingBox& bb,BillboardVegetationObjectVector& object_list, double lod_density, double lod_scale)
	{
		osg::Vec3 origin = bb._min; 
		osg::Vec3 size = bb._max - bb._min; 

		unsigned int num_objects_to_create = size.x()*size.y()*layer.Density*lod_density;
		object_list.reserve(object_list.size()+num_objects_to_create);

		for(unsigned int i=0;i<num_objects_to_create;++i)
		{
			osg::Vec3 pos(Utils::random(origin.x(),origin.x()+size.x()),Utils::random(origin.y(),origin.y()+size.y()),0);
			osg::Vec3 inter;
			osg::Vec4 color;
			osg::Vec4 mat_color;
			float rand_int = Utils::random(layer.ColorIntensity.x(),layer.ColorIntensity.y());
			osg::Vec3 offset_pos = pos + m_Offset;
			if(m_TerrainQuery->getTerrainData(offset_pos,color,mat_color,inter))
			{
				if(layer.hasMaterial(mat_color))
				{
					BillboardObject* veg_obj = new BillboardObject;
					//TODO add color to layer
					float tree_scale = Utils::random(layer.Scale.x() ,layer.Scale.y())*lod_scale;
					veg_obj->Width = Utils::random(layer.Width.x(),layer.Width.y())*tree_scale;
					veg_obj->Height = Utils::random(layer.Height.x(),layer.Height.y())*tree_scale;
					veg_obj->TextureIndex = layer._TextureIndex;
					veg_obj->Position = inter-m_Offset;
					veg_obj->Color = color*rand_int*0.5;
					veg_obj->Color += osg::Vec4(1,1,1,1)*0.5*rand_int;
					object_list.push_back(veg_obj);
				}
			}
		}
	}
	
	BillboardVegetationObjectVector QuadTreeScattering::_generateVegetation(BillboardLayerVector &layers,const osg::BoundingBox& bb, double lod_density,double lod_scale)
	{
		BillboardVegetationObjectVector trees;
		double const density= 1;
		for(size_t i = 0 ; i < layers.size();i++)
		{
			_populateVegetationLayer(layers[i],bb,trees,lod_density,lod_scale);
		}
		return trees;
	}

	std::string QuadTreeScattering::_createFileName( unsigned int lv,	unsigned int x, unsigned int y )
	{
		std::stringstream sstream;
		sstream << m_FilenamePrefix << lv << "_X" << x << "_Y" << y << ".ive";
		return sstream.str();
	}

	osg::Node* QuadTreeScattering::_createLODRec(int ld, BillboardLayerVector &layers, BillboardVegetationObjectVector trees, const osg::BoundingBox &bb,int x, int y)
	{
		if(ld < 6) //only show progress above lod 6, we dont want to spam the log
			std::cout << "Progress:" << (int)(100.0f*((float) m_CurrentTile/(float) m_NumberOfTiles)) <<  "% Create Tile:" << m_CurrentTile << " of:" << m_NumberOfTiles << std::endl;
		m_CurrentTile++;

		osg::ref_ptr<osg::Group> children_group = new osg::Group;

		//mesh_group is returned so we don't use smart pointer
		osg::Group* mesh_group = new osg::Group;
		double bb_size = (bb._max.x() - bb._min.x());
		//bool final_lod = false;
		//if(bb_size < m_MinTileSize)
		bool final_lod = (ld == m_FinalLOD);
		
		//if(bb_size < m_ViewDistance)
		if(ld >= m_StartLOD)
		{
			//calculate density ratio for this LOD level
			float lod = ld - m_StartLOD;
			float inv_lod = m_FinalLOD - ld;
			double lod_density = pow(m_DensityLODRatio, inv_lod);
			double lod_scale = pow(m_ScaleLODRatio, lod);
			
			
			/*int inv_lod = (m_FinalLOD - ld);
			int tiles = pow(2.0, inv_lod);
			double density_ratio = tiles*tiles;
			density_ratio = 1.0/density_ratio;*/

			if(trees.size() == 0)
			{
				trees = _generateVegetation(layers,bb,lod_density,lod_scale);
			}
			BillboardVegetationObjectVector patch_trees;
			
			//get trees inside patch
			/*for(size_t i = 0; i < trees.size(); i = i++)
			{
				//if(bb.contains(trees[i]->Position+m_Offset))
				{
					patch_trees.push_back(trees[i]);
				}
			}*/
			
			osg::Node* node = m_VRT->create(trees,bb);
			trees.clear();

			mesh_group->addChild(node);

			//debug
			//osgDB::writeNodeFile(*node,"c:/temp/bbveg.ive");
		}

		//split bounding box into four new children

		if(!final_lod)
		{
			double sx = (bb._max.x() - bb._min.x())*0.5;
			double sy = (bb._max.x() - bb._min.x())*0.5;
			osg::BoundingBox b1(bb._min, 
				osg::Vec3(bb._min.x() + sx,  bb._min.y() + sy  ,bb._max.z()));
			osg::BoundingBox b2(osg::Vec3(bb._min.x() + sx , bb._min.y()       ,bb._min.z()),
				osg::Vec3(bb._max.x(),       bb._min.y() + sy  ,bb._max.z()));

			osg::BoundingBox b3(osg::Vec3(bb._min.x() + sx,  bb._min.y() + sy   ,bb._min.z()),
				osg::Vec3(bb._max.x(),       bb._max.y()		,bb._max.z()));

			osg::BoundingBox b4(osg::Vec3(bb._min.x(),		 bb._min.y() + sy  ,bb._min.z()),
				osg::Vec3(bb._min.x() + sx,  bb._max.y()		,bb._max.z()));

			//first check that we are inside initial bounding box
			if(b1.intersects(m_InitBB))	children_group->addChild( _createLODRec(ld+1,layers,trees,b1, x*2,   y*2));
			if(b2.intersects(m_InitBB))	children_group->addChild( _createLODRec(ld+1,layers,trees,b2, x*2,   y*2+1));
			if(b3.intersects(m_InitBB)) children_group->addChild( _createLODRec(ld+1,layers,trees,b3, x*2+1, y*2+1));
			if(b4.intersects(m_InitBB)) children_group->addChild( _createLODRec(ld+1,layers,trees,b4, x*2+1, y*2)); 

			if(m_UsePagedLOD)
			{
				osg::PagedLOD* plod = new osg::PagedLOD;
				plod->setCenterMode( osg::PagedLOD::USER_DEFINED_CENTER );
				plod->setCenter(bb.center());
				double radius = sqrt(bb_size*bb_size);
				plod->setRadius(radius);
				float cutoff = radius*2;
				//regular terrain LOD setup
				//plod->addChild(mesh_group, cutoff, FLT_MAX );
				int c_index = 0;
				if(mesh_group->getNumChildren() > 0)
				{
					plod->addChild(mesh_group, 0, FLT_MAX );	
					c_index++;
				}
				const std::string filename = _createFileName(ld, x,y);
				plod->setFileName( c_index, filename );
				plod->setRange(c_index,0,cutoff);
				osgDB::writeNodeFile( *children_group, m_SavePath + filename );

				
				return plod;
			}
			else
			{
				osg::LOD* plod = new osg::LOD;
				plod->setCenterMode(osg::PagedLOD::USER_DEFINED_CENTER);
				plod->setCenter( bb.center());

				double radius = sqrt(bb_size*bb_size);
				plod->setRadius(radius);

				float cutoff = radius*2;
				//regular terrain LOD setup
				//plod->addChild(mesh_group, cutoff, FLT_MAX );
				plod->addChild(mesh_group, 0, FLT_MAX );
				plod->addChild(children_group, 0.0f, cutoff );
				return plod;
			}
		}
		else
			return mesh_group;
		
	}
	osg::Node* QuadTreeScattering::create(BillboardData &data, const std::string &page_lod_path, const std::string &filename_prefix)
	{
		m_FilenamePrefix = filename_prefix;
		if(page_lod_path != "")
		{
			m_UsePagedLOD = true;
			m_SavePath = page_lod_path;
		}
		
		delete m_VRT;
		m_VRT = new BRTShaderInstancing(data);

		osg::ComputeBoundsVisitor  cbv;
		osg::BoundingBox &bb(cbv.getBoundingBox());
		m_Terrain->accept(cbv);
	
		m_ViewDistance = data.ViewDistance;
		m_VRT->setAlphaRefValue(data.AlphaRefValue);
		m_VRT->setAlphaBlend(data.UseAlphaBlend);
		m_VRT->setTerrainNormal(data.TerrainNormal);
		
		//get max terrain side, we want square area for to begin quad tree splitting
		double terrain_size = std::max(bb._max.x() - bb._min.x(), bb._max.y() - bb._min.y());
		
		m_Offset = bb._min;
		m_InitBB._min.set(0,0,0);
		m_InitBB._max = bb._max - bb._min;
		
		bb._max.set(terrain_size, terrain_size, bb._max.z() - bb._min.z());
		bb._min.set(0,0,0);

		//add offset matrix
		osg::MatrixTransform* transform = new osg::MatrixTransform;
		transform->setMatrix(osg::Matrix::translate(m_Offset));

		//save for later...
		m_DensityLODRatio = data.DensityLODRatio;
		m_ScaleLODRatio = data.ScaleLODRatio;

		//Calculate min tile size based on view distance and number density lod levels 
		//m_MinTileSize = m_ViewDistance/(data.MaxDensityLODs+1);

		double temp_size  = terrain_size;
		m_FinalLOD =0;
		m_StartLOD =0;
		m_NumberOfTiles = 0;
		m_CurrentTile = 0;

		while(temp_size > m_ViewDistance)
		{
			m_StartLOD++;
			temp_size = temp_size/2.0;
		}
		m_FinalLOD = m_StartLOD + data.MaxDensityLODs;

		int ld = 0;
		while(ld < m_FinalLOD)
		{
			ld++;
			int side = 2 << ld;
			m_NumberOfTiles += side*side; 
		}

		BillboardVegetationObjectVector trees;
		osg::Node* outnode = _createLODRec(0, data.Layers, trees, bb,0,0);
		outnode->setStateSet((osg::StateSet*) m_VRT->getStateSet()->clone(osg::CopyOp::DEEP_COPY_STATESETS));
		if(m_UsePagedLOD)
		{
			transform->addChild(outnode);
			osgDB::writeNodeFile(*transform, m_SavePath + m_FilenamePrefix + "master.ive");
			//osg::ProxyNode* pn = new osg::ProxyNode();
			//pn->setFileName(0,"master.ive");
			//pn->setDatabasePath("C:/temp/paged");
			//transform->addChild(pn);
			//osgDB::writeNodeFile( *transform, m_SavePath + "/transformation.osg" );
		}
		else
		{
			transform->addChild(outnode);
		}
		return transform;
	}
}
