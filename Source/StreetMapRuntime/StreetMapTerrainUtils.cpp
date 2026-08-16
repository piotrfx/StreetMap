#include "StreetMapTerrainUtils.h"
#include "LandscapeProxy.h"

bool StreetMapTerrainUtils::TrySampleLocalZ( ALandscapeProxy* Landscape, const FTransform& OwnerTransform, const FVector2D& LocalXY, float& OutLocalZ )
{
	if( Landscape == nullptr )
	{
		return false;
	}

	const FVector WorldXY = OwnerTransform.TransformPosition( FVector( LocalXY.X, LocalXY.Y, 0.0 ) );

	const TOptional<float> WorldZ = Landscape->GetHeightAtLocation( WorldXY );
	if( !WorldZ.IsSet() )
	{
		return false;
	}

	const FVector LocalPoint = OwnerTransform.InverseTransformPosition( FVector( WorldXY.X, WorldXY.Y, (double)WorldZ.GetValue() ) );
	OutLocalZ = (float)LocalPoint.Z;
	return true;
}
