"""GIS/spatial helper backend for GraphVis."""
from __future__ import annotations

from dataclasses import dataclass, field
from io import BytesIO
from pathlib import Path
from typing import Any, Mapping

import numpy as np
import pandas as pd


@dataclass(slots=True)
class GISLayer:
    name: str
    data: Any
    crs: str | None = None
    metadata: dict[str,Any] = field(default_factory=dict)


class GISEngine:
    @staticmethod
    def load_vector(path: str) -> GISLayer:
        try:
            import geopandas as gpd
        except Exception as exc: raise RuntimeError("Shapefile/GeoJSON support requires geopandas.") from exc
        gdf=gpd.read_file(path); return GISLayer(Path(path).stem,gdf,str(gdf.crs) if gdf.crs else None,{"source":str(Path(path).resolve())})

    @staticmethod
    def points(frame: pd.DataFrame, longitude: str, latitude: str, crs: str = "EPSG:4326") -> GISLayer:
        try:
            import geopandas as gpd
        except Exception as exc: raise RuntimeError("Spatial point layers require geopandas.") from exc
        work=frame.dropna(subset=[longitude,latitude]).copy(); gdf=gpd.GeoDataFrame(work,geometry=gpd.points_from_xy(work[longitude],work[latitude]),crs=crs); return GISLayer("points",gdf,crs)

    @staticmethod
    def fetch_wms(base_url: str, layer: str, bbox: tuple[float,float,float,float], width: int = 1024, height: int = 768, crs: str = "EPSG:4326", image_format: str = "image/png", version: str = "1.3.0") -> bytes:
        try: import requests
        except Exception as exc: raise RuntimeError("WMS overlays require requests.") from exc
        params={"service":"WMS","request":"GetMap","version":version,"layers":layer,"styles":"","format":image_format,"transparent":"true","width":width,"height":height,"bbox":",".join(map(str,bbox))}
        params["crs" if version.startswith("1.3") else "srs"]=crs
        r=requests.get(base_url,params=params,timeout=30); r.raise_for_status(); return r.content

    @staticmethod
    def add_basemap(ax: Any, crs: str | None = None, source: Any = None) -> None:
        try: import contextily as ctx
        except Exception as exc: raise RuntimeError("Online basemaps require contextily.") from exc
        ctx.add_basemap(ax,crs=crs,source=source or ctx.providers.OpenStreetMap.Mapnik)

    @staticmethod
    def ternary_to_cartesian(a: np.ndarray,b: np.ndarray,c: np.ndarray) -> tuple[np.ndarray,np.ndarray]:
        a,b,c=np.asarray(a,float),np.asarray(b,float),np.asarray(c,float); s=a+b+c; s=np.where(s==0,1,s); a,b,c=a/s,b/s,c/s; x=b+0.5*c; y=np.sqrt(3)/2*c; return x,y
