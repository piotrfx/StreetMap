#pragma once

#include "CoreMinimal.h"

class ALandscapeProxy;

namespace StreetMapTerrainUtils
{
	/** Samples Landscape's height at LocalXY (interpreted in OwnerTransform's local space, Z ignored),
	 *  and writes the result back into OutLocalZ (also OwnerTransform's local space). Returns false
	 *  (OutLocalZ untouched) if Landscape is null or LocalXY falls outside its bounds -- callers should
	 *  fall back to the legacy flat behavior in that case. */
	bool TrySampleLocalZ( ALandscapeProxy* Landscape, const FTransform& OwnerTransform, const FVector2D& LocalXY, float& OutLocalZ );
}
