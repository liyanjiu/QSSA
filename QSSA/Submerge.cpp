/*
* gdal_image.cpp -- Load GIS data into OpenCV Containers using the Geospatial Data Abstraction Library
*/

#include "submerge.h"

Submerge::Submerge()
{
	// default 
	m_matchMethod = BASE_GEOGCS;
	m_submergeMethod = PASSIVE_SUBMERGING;
	
	// define the color range to create our output DEM heat map
	// Pair format ( Color, elevation );  Push from low to high
	// Note:  This would be perfect for a configuration file, but is here for a working demo.
	// BGR
	color_range.push_back(std::pair<cv::Vec3b, double>(cv::Vec3b(188, 154, 46), -1));//-1
	color_range.push_back(std::pair<cv::Vec3b, double>(cv::Vec3b(110, 220, 110), 0.25));
	color_range.push_back(std::pair<cv::Vec3b, double>(cv::Vec3b(150, 250, 230), 20));
	color_range.push_back(std::pair<cv::Vec3b, double>(cv::Vec3b(160, 220, 200), 75));
	color_range.push_back(std::pair<cv::Vec3b, double>(cv::Vec3b(220, 190, 170), 100));
	color_range.push_back(std::pair<cv::Vec3b, double>(cv::Vec3b(250, 180, 140), 200));
	// RGB
	color_submerge.push_back(std::pair<cv::Vec3b, double>(cv::Vec3b(0, 0, 60), 10));
	color_submerge.push_back(std::pair<cv::Vec3b, double>(cv::Vec3b(60, 60, 0), 20));
	color_submerge.push_back(std::pair<cv::Vec3b, double>(cv::Vec3b(60, 30, 0), 50));
	color_submerge.push_back(std::pair<cv::Vec3b, double>(cv::Vec3b(60, 0, 0), 100));
}
Submerge::~Submerge()
{

}

bool Submerge::readConfig()
{
	ifstream config("Config/submerge-color.txt");
	if (!config)
	{
		return false;
	}
	else
	{
		std::pair<cv::Vec3b, double> line;
		double dz;
		uchar b, g, r;
		char buffer[20];
		while (!config.eof())
		{	
			config.getline(buffer, 20);
			sscanf(buffer, "%d %d %d %d", b, g, r, dz);

			line.second = dz;
			line.first[0] = b;
			line.first[1] = g;
			line.first[2] = r;
			color_submerge.push_back(line);
		}
		config.close();
		return true;
	}
}

/*
* Linear Interpolation
* p1 - Point 1
* p2 - Point 2
* t  - Ratio from Point 1 to Point 2
*/
cv::Point2d Submerge::lerp(cv::Point2d const& p1, cv::Point2d const& p2, const double& t)
{
	return cv::Point2d(((1 - t)*p1.x) + (t*p2.x),
		((1 - t)*p1.y) + (t*p2.y));
}

/*
* Interpolate Colors
*/

cv::Vec3b Submerge::lerp(cv::Vec3b const& minColor,
	cv::Vec3b const& maxColor,
	double const& t)
{
	cv::Vec3b output;
	for (int i = 0; i<3; i++)
	{
		output[i] = (uchar)(((1 - t)*minColor[i]) + (t * maxColor[i]));
	}
	return output;
}

/*
* Compute the dem color
*/
cv::Vec3b Submerge::get_dem_color(const double& elevation) {

	// if the elevation is below the minimum, return the minimum
	if (elevation < color_range[0].second) {
		return color_range[0].first;
	}
	// if the elevation is above the maximum, return the maximum
	if (elevation > color_range.back().second) {
		return color_range.back().first;
	}

	// otherwise, find the proper starting index
	int idx = 0;
	double t = 0;
	for (int x = 0; x<(int)(color_range.size() - 1); x++) {

		// if the current elevation is below the next item, then use the current
		// two colors as our range
		if (elevation < color_range[x + 1].second) {
			idx = x;
			t = (color_range[x + 1].second - elevation) /
				(color_range[x + 1].second - color_range[x].second);

			break;
		}
	}

	// interpolate the color
	return lerp(color_range[idx].first, color_range[idx + 1].first, t);
}

///*
//* Convert a pixel coordinate to UTM coordinates
//*/
//cv::Point2d pixel2utm(const int &x, const int &y, const cv::Size &size)
//{
//	// compute the ratio of the pixel location to its dimension
//	double rx = (double)x / size.width;
//	double ry = (double)y / size.height;
//
//	// compute LERP of each coordinate
//	cv::Point2d rightSide = lerp(tr, br, ry);
//	cv::Point2d leftSide = lerp(tl, bl, ry);
//
//	// compute the actual Lat/Lon coordinate of the interpolated coordinate
//	return lerp(leftSide, rightSide, rx);
//}


/*
* Convert a pixel coordinate to world coordinates
*/
cv::Point2d Submerge::pixel2world(const int& x, const int& y, const cv::Size& size) {

	// compute the ratio of the pixel location to its dimension
	double rx = (double)x / size.width;
	double ry = (double)y / size.height;

	// compute LERP of each coordinate
	cv::Point2d rightSide = lerp(landsat_tr, landsat_br, ry);
	cv::Point2d leftSide = lerp(landsat_tl, landsat_bl, ry);

	// compute the actual Lat/Lon coordinate of the interpolated coordinate
	return lerp(leftSide, rightSide, rx);
}

/*
* Given a pixel coordinate and the size of the input image, compute the pixel location
* on the DEM image.
*/
cv::Point2d Submerge::world2dem(cv::Point2d const& coordinate, const cv::Size& dem_size) {


	// relate this to the dem points
	// ASSUMING THAT DEM DATA IS ORTHORECTIFIED
	double demRatioX = 1- ((dem_tr.x - coordinate.x) / (dem_tr.x - dem_bl.x));
	double demRatioY = ((dem_tr.y - coordinate.y) / (dem_tr.y - dem_bl.y));

	cv::Point2d output;
	output.x = demRatioX * dem_size.width;
	output.y = demRatioY * dem_size.height;

	return output;
}

/*
* Add color to a specific pixel color value
*/
void Submerge::add_color(cv::Vec3b& pix, const uchar& b, const uchar& g, const uchar& r) {
	
	if (pix[0] + b < 255 && pix[0] + b >= 0) { pix[0] += b; }
	if (pix[1] + g < 255 && pix[1] + g >= 0) { pix[1] += g; }
	if (pix[2] + r < 255 && pix[2] + r >= 0) { pix[2] += r; }
}
void Submerge::add_color(cv::Vec3b &pix, cv::Vec3b color)
{
	if (pix[0] + color[0] < 255 && pix[0] + color[0] >= 0) { pix[0] += color[0]; }
	if (pix[1] + color[1] < 255 && pix[1] + color[1] >= 0) { pix[1] += color[1]; }
	if (pix[2] + color[2] < 255 && pix[2] + color[2] >= 0) { pix[2] += color[2]; }
}

/*
* Main Submerging Function
*/
bool Submerge::run()
{	
	// check file
	CPLAssert(m_landsat != nullptr);
	CPLAssert(m_dem != nullptr);

	// get landsat and DEM OGR Spatial Referencce System
	OGRSpatialReference landsatSRS;
	OGRSpatialReference demSRS;
	char *wkt = const_cast<char *>(m_landsat->m_dataset->GetProjectionRef());
	landsatSRS.importFromWkt(&wkt);

	wkt = const_cast<char *>(m_dem->m_dataset->GetProjectionRef());
	demSRS.importFromWkt(&wkt);

	// switch match method used
	switch (m_matchMethod)
	{
	case Submerge::BASE_GEOGCS:
		if (landsatSRS.IsGeographic() && demSRS.IsGeographic())
		{
			if (m_submergeMethod == PASSIVE_SUBMERGING)
			{
				runWithCRSPsv();
			}
			else
			{
				runWithCRSPsv();
			}
			
			return true;
		}
		else
		{
			QMessageBox::critical(this, tr("Error!"), tr("The CRS of the selected files are not Geographic Coordinate System. Please check the information of each file."));
			return false;
		}
		break;
	case Submerge::BASE_PROJCS:
		if (landsatSRS.IsProjected() && demSRS.IsProjected())
		{
			if (m_submergeMethod == PASSIVE_SUBMERGING)
			{
				runWithCRSPsv();
			}
			else
			{
				runWithCRSPsv();
			}
			return true;
		}
		else
		{
			QMessageBox::critical(this, tr("Error!"), tr("The CRS of the selected files are not Projected Coordinate System. Please check the information of each file."));
			return false;
		}
		break;
	case Submerge::BASE_SIFT:
		//runWithSIFT();
		break;
	case Submerge::BASE_SURF:
		//runWithSURF();
		break;
	default:
		break;
	}

	// ready to submerge
	/*const char *landsatProjection = landsatSRS.GetAttrValue("GEOGCS");
	const char *demProjection = demSRS.GetAttrValue("GEOGCS");
	QMessageBox::about(this,
		tr("Submerge Information"),
		tr("<p>Landsat file coordinate reference system (CRS): <b>%1</b>. </p> "
			"<p>DEM file coordinate reference system(CRS): <b>%2</b>. </p> ")
		.arg(landsatProjection)
		.arg(demProjection));*/
	
	// check oSRS value


	/*if (landsatSRS->IsProjected())
	{
		
		if (landsatProjection == NULL)
		{
			if (poSRS->IsGeographic())
				sprintf(szProj4 + strlen(szProj4), "+proj=longlat ");
			else
				sprintf(szProj4 + strlen(szProj4), "unknown ");
		}
		else if (EQUAL(pszProjection, SRS_PT_CYLINDRICAL_EQUAL_AREA))
		{
			sprintf(szProj4 + strlen(szProj4),
				"+proj=cea +lon_0=%.9f +lat_ts=%.9f +x_0=%.3f +y_0=%.3f ",
				poSRS->GetProjParm(SRS_PP_CENTRAL_MERIDIAN, 0.0),
				poSRS->GetProjParm(SRS_PP_STANDARD_PARALLEL_1, 0.0),
				poSRS->GetProjParm(SRS_PP_FALSE_EASTING, 0.0),
				poSRS->GetProjParm(SRS_PP_FALSE_NORTHING, 0.0));
		}
	}


	if (m_matchMethod == BASE_PROJCS)
	{
		//
	}
	else if (m_matchMethod == BASE_GEOGCS)
	{

	}*/
	return 0;
}

bool Submerge::runWithCRSPsv()
{
	// define the corner points of landsat 
	landsat_tl.x = m_landsat->m_origin.first;//0
	landsat_tl.y = m_landsat->m_origin.second;//3

	landsat_br.x = landsat_tl.x + m_landsat->m_pixelSize.first * m_landsat->m_width
								+ m_landsat->m_adfGeoTransform[2] * m_landsat->m_height;

	landsat_br.y = landsat_tl.y + m_landsat->m_adfGeoTransform[2] * m_landsat->m_width
								+ m_landsat->m_pixelSize.second * m_landsat->m_height;

	landsat_tr.x = landsat_br.x;
	landsat_tr.y = landsat_tl.y;

	landsat_bl.x = landsat_tl.x;
	landsat_bl.y = landsat_br.y;

	// define the corner points of dem
	dem_bl.x = m_dem->m_origin.first;
	dem_tr.y = m_dem->m_origin.second;

	dem_bl.y = dem_tr.y + m_dem->m_adfGeoTransform[2] * m_dem->m_width
							+ m_dem->m_pixelSize.second * m_dem->m_height;

	dem_tr.x = dem_bl.x + m_dem->m_pixelSize.first * m_dem->m_width
							+ m_dem->m_adfGeoTransform[2] * m_dem->m_height;
	
	// create output
	cv::Mat output_dem(m_landsat->m_image.size(), CV_8UC3);
	cv::Mat output_dem_flood(m_landsat->m_image.size(), CV_8UC3);

	// define a minimum elevation
	double minElevation = -10;//-10

	// iterate over each pixel in the image, computing the dem point
	for (int y = 0; y<m_landsat->m_image.rows; y++) {
		emit submergeProgress(y);
		for (int x = 0; x<m_landsat->m_image.cols; x++) {

			// convert the pixel coordinate to lat/lon coordinates
			cv::Point2d coordinate = pixel2world(x, y, m_landsat->m_image.size());

			// compute the dem image pixel coordinate from lat/lon
			cv::Point2d dem_coordinate = world2dem(coordinate, m_dem->m_image.size());

			// extract the elevation
			double dz;
			if (dem_coordinate.x >= 0 && dem_coordinate.y >= 0 &&
				dem_coordinate.x < m_dem->m_image.cols && dem_coordinate.y < m_dem->m_image.rows) {
				dz = m_dem->m_image.at<short>(dem_coordinate);
			}
			else {
				dz = minElevation;
			}

			// write the pixel value to the file
			output_dem_flood.at<cv::Vec3b>(y, x) = m_landsat->m_image.at<cv::Vec3b>(y, x);

			// compute the color for the heat map output
			cv::Vec3b actualColor = get_dem_color(dz);
			output_dem.at<cv::Vec3b>(y, x) = actualColor;

			// show effect of a 10 meter increase in ocean levels
			if (dz < color_submerge[0].second) {
				add_color(output_dem_flood.at<cv::Vec3b>(y, x), color_submerge[0].first);
			}
			else if (dz < color_submerge[1].second) {
				add_color(output_dem_flood.at<cv::Vec3b>(y, x), color_submerge[1].first);
			}
			// show effect of a 50 meter increase in ocean levels
			else if (dz < color_submerge[2].second) {
				add_color(output_dem_flood.at<cv::Vec3b>(y, x), color_submerge[2].first);
			}
			// show effect of a 100 meter increase in ocean levels
			else if (dz < color_submerge[3].second) {
				add_color(output_dem_flood.at<cv::Vec3b>(y, x), color_submerge[3].first);
			}
		}
	}

	// print our heat map
	QFileInfo landFI(m_dem->m_filename);
	QString heatmapDstName = "Data/Output/" + landFI.baseName() + "_heatmpa.jpg";
	cv::imwrite(heatmapDstName.toStdString(), output_dem);

	// print the flooding effect image
	QFileInfo demFI(m_landsat->m_filename);
	QString floodDstName = "Data/Output/" + demFI.baseName() + "_flood.jpg";
	Mat output_dem_flood_write;
	cvtColor(output_dem_flood, output_dem_flood_write, CV_RGB2BGR);
	cv::imwrite(floodDstName.toStdString(), output_dem_flood_write);

	emit submergeFinish();
	return true;
	/*QMessageBox::about(this,
		tr("Submerge Information"),
		tr("(%1,%2) "
			"(%3,%4) "
			"(%5,%6) "
			"(%7,%8) ")
		.arg(landsat_tl.x).arg(landsat_tl.y)
		.arg(landsat_bl.x).arg(landsat_bl.y)
		.arg(landsat_tr.x).arg(landsat_tr.y)
		.arg(landsat_br.x).arg(landsat_br.y));*/
}

//bool Submerge::runWithCRSAct()
//{
//	// define the corner points of landsat 
//	landsat_tl.x = m_landsat->m_origin.first;//0
//	landsat_tl.y = m_landsat->m_origin.second;//3
//
//	landsat_br.x = landsat_tl.x + m_landsat->m_pixelSize.first * m_landsat->m_width
//		+ m_landsat->m_adfGeoTransform[2] * m_landsat->m_height;
//
//	landsat_br.y = landsat_tl.y + m_landsat->m_adfGeoTransform[2] * m_landsat->m_width
//		+ m_landsat->m_pixelSize.second * m_landsat->m_height;
//
//	landsat_tr.x = landsat_br.x;
//	landsat_tr.y = landsat_tl.y;
//
//	landsat_bl.x = landsat_tl.x;
//	landsat_bl.y = landsat_br.y;
//
//	// define the corner points of dem
//	dem_bl.x = m_dem->m_origin.first;
//	dem_tr.y = m_dem->m_origin.second;
//
//	dem_bl.y = dem_tr.y + m_dem->m_adfGeoTransform[2] * m_dem->m_width
//		+ m_dem->m_pixelSize.second * m_dem->m_height;
//
//	dem_tr.x = dem_bl.x + m_dem->m_pixelSize.first * m_dem->m_width
//		+ m_dem->m_adfGeoTransform[2] * m_dem->m_height;
//
//	// create output
//	cv::Mat output_dem(m_landsat->m_image.size(), CV_8UC3);
//	cv::Mat output_dem_flood(m_landsat->m_image.size(), CV_8UC3);
//
//	// define a minimum elevation
//	double minElevation = -10;
//	cv::Point2d seedPoint;
//	double dz = -10;
//	cv::Mat isSubmerge(m_landsat->m_image.size(), CV_8UC1);
//
//	// iterate over each pixel in the image, computing the first seed point
//	for (int y = 0; y < m_landsat->m_image.rows && dz != 0; y++) {
//		for (int x = 0; x < m_landsat->m_image.cols && dz != 0; x++) {
//			// convert the pixel coordinate to lat/lon coordinates
//			cv::Point2d coordinate = pixel2world(x, y, m_landsat->m_image.size());
//
//			// compute the dem image pixel coordinate from lat/lon
//			cv::Point2d dem_coordinate = world2dem(coordinate, m_dem->m_image.size());
//
//			// extract the elevation and seek for seed point
//			if (dem_coordinate.x >= 0 && dem_coordinate.y >= 0 &&
//				dem_coordinate.x < m_dem->m_image.cols && dem_coordinate.y < m_dem->m_image.rows) {
//				dz = m_dem->m_image.at<short>(dem_coordinate);
//				if (dz == 0) { seedPoint = cv::Point2d(x, y); }
//			}
//			else {
//				dz = minElevation;
//			}
//		}
//	}
//
//	// iterate over each pixel in the image, computing dem point
//	for (int y = seedPoint.y; y < m_landsat->m_image.rows; y++)
//	{
//		for (int x = seedPoint.x; x < m_landsat->m_image.cols; x++)
//		{
//			if (y - 1 > 0 && y + 1 < m_landsat->m_image.rows &&
//				x - 1 > 0 && x + 1 < m_landsat->m_image.cols)
//			{
//				cv::Point2i tl = (x - 1, y - 1);
//				cv::Point2i tm = (x    , y - 1);
//				cv::Point2i tr = (x + 1, y - 1);
//				cv::Point2i ml = (x - 1, y    );
//				cv::Point2i mr = (x + 1, y    );
//				cv::Point2i bl = (x - 1, y + 1);
//				cv::Point2i bm = (x    , y + 1);
//				cv::Point2i br = (x + 1, y + 1);
//				vector<Point2i> around;
//				around.push_back(tl);
//				around.push_back(tm);
//				around.push_back(tr);
//				around.push_back(ml);
//				around.push_back(mr);
//				around.push_back(bl);
//				around.push_back(bm);
//				around.push_back(br);
//				//分别计算dz，开始画色
//				//计算下一个点
//				//cv::Point2d nextSeed = findSeed(around);
//				
//			}
//			
//			
//		}
//	}
//}
//Point2d findSeed(vector<Point2i> around)
//{
//	/*cv::Point2d coordinate = pixel2world(x, y, m_landsat->m_image.size());
//	cv::Point2d dem_coordinate = world2dem(coordinate, m_dem->m_image.size());
//	dz = m_dem->m_image.at<short>(dem_coordinate);*/
//
//	/*if (dem_coordinate.x >= 0 && dem_coordinate.y >= 0 &&
//	dem_coordinate.x < m_dem->m_image.cols && dem_coordinate.y < m_dem->m_image.rows) {
//	dz = m_dem->m_image.at<short>(dem_coordinate);
//	}*/
//}