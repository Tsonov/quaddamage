/***************************************************************************
 *   Copyright (C) 2009-2015 by Veselin Georgiev, Slavomir Kaslev et al    *
 *   admin@raytracing-bg.net                                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
/**
 * @File mesh.cpp
 * @Brief Contains implementation of the Mesh class
 */

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "mesh.h"
#include "constants.h"
#include "color.h"
using std::max;
using std::vector;
using std::string;


void Mesh::beginRender()
{
	computeBoundingGeometry();
}

void Mesh::computeBoundingGeometry()
{
	Vector O(0, 0, 0);
	
	for (auto& v: vertices) {
		O += v;
	}
	
	O = O / vertices.size();
	
	double maxRadius = 0;
	for (auto&v : vertices) {
		double distance = (O - v).length();
		maxRadius = max(maxRadius, distance);
	}
	
	boundingGeom = new Sphere(O, maxRadius);
}

Mesh::~Mesh()
{
	if (boundingGeom) delete boundingGeom;
}

inline double det(const Vector& a, const Vector& b, const Vector& c)
{
	return (a^b) * c;
}

bool Mesh::intersectTriangle(const Ray& ray, const Triangle& t, IntersectionInfo& info)
{
	if (dot(ray.dir, t.gnormal) > 0) return false;
	Vector A = vertices[t.v[0]];
	Vector B = vertices[t.v[1]];
	Vector C = vertices[t.v[2]];
	
	Vector H = ray.start - A;
	Vector D = ray.dir;
	
	double Dcr = det(B-A, C-A, -D);
	if (fabs(Dcr) < 1e-12) return false;
	
	double lambda2 = det(H, C-A, -D) / Dcr;
	double lambda3 = det(B-A, H, -D) / Dcr;
	
	if (lambda2 < 0 || lambda3 < 0) return false;
	if (lambda2 > 1 || lambda3 > 1) return false;
	if (lambda2 + lambda3 > 1) return false;
	
	double gamma = det(B-A, C-A, H) / Dcr;
	
	if (gamma < 0) return false;
	
	info.distance = gamma;
	info.ip = ray.start + ray.dir * gamma;
	if (!faceted) {
		Vector nA = normals[t.n[0]];
		Vector nB = normals[t.n[1]];
		Vector nC = normals[t.n[2]];
		
		info.normal = nA + (nB - nA) * lambda2 + (nC - nA) * lambda3;
		info.normal.normalize();
	} else {
		info.normal = t.gnormal;
	}
	
	info.dNdx = t.dNdx;
	info.dNdy = t.dNdy;
			
	Vector uvA = uvs[t.t[0]];
	Vector uvB = uvs[t.t[1]];
	Vector uvC = uvs[t.t[2]];
	
	Vector uv = uvA + (uvB - uvA) * lambda2 + (uvC - uvA) * lambda3;
	info.u = uv.x;
	info.v = uv.y;
	info.geom = this;
	
	return true;
}


bool Mesh::intersect(const Ray& ray, IntersectionInfo& info)
{
	if (boundingGeom->intersect(ray, info) == false)
		return false;
	bool found = false;
	IntersectionInfo closestInfo;
	
	closestInfo.distance = INF;
	
	for (auto& T: triangles) {
		if (intersectTriangle(ray, T, info) &&
			info.distance < closestInfo.distance) {
				found = true;
				closestInfo = info;
		}
	}
	
	if (found)
		info = closestInfo;
	return found;
}

static int toInt(const string& s)
{
	if (s.empty()) return 0;
	int x;
	if (1 == sscanf(s.c_str(), "%d", &x)) return x;
	return 0;
}

static double toDouble(const string& s)
{
	if (s.empty()) return 0;
	double x;
	if (1 == sscanf(s.c_str(), "%lf", &x)) return x;
	return 0;
}

static void parseTrio(string s, int& vertex, int& uv, int& normal)
{
	vector<string> items = split(s, '/');
	// "4" -> {"4"} , "4//5" -> {"4", "", "5" }
	
	vertex = toInt(items[0]);
	uv = items.size() >= 2 ? toInt(items[1]) : 0;
	normal = items.size() >= 3 ? toInt(items[2]) : 0;
}

static Triangle parseTriangle(string s0, string s1, string s2)
{
	// "3", "3/4", "3//5", "3/4/5"  (v/uv/normal)
	Triangle T;
	parseTrio(s0, T.v[0], T.t[0], T.n[0]);
	parseTrio(s1, T.v[1], T.t[1], T.n[1]);
	parseTrio(s2, T.v[2], T.t[2], T.n[2]);
	return T;
}

static void solve2D(Vector A, Vector B, Vector C, double& x, double& y)
{
	// solve: x * A + y * B = C
	double mat[2][2] = { { A.x, B.x }, { A.y, B.y } };
	double h[2] = { C.x, C.y };
	
	double Dcr = mat[0][0] * mat[1][1] - mat[1][0] * mat[0][1];
	x =         (     h[0] * mat[1][1] -      h[1] * mat[0][1]) / Dcr;
	y =         (mat[0][0] *      h[1] - mat[1][0] *      h[0]) / Dcr;
}

bool Mesh::loadFromOBJ(const char* filename)
{
	FILE* f = fopen(filename, "rt");
	
	if (!f) return false;
	
	vertices.push_back(Vector(0, 0, 0));
	uvs.push_back(Vector(0, 0, 0));
	normals.push_back(Vector(0, 0, 0));
	
	char line[10000];
	
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#') continue;
	
		vector<string> tokens = tokenize(line); 
		// "v 0 1    4" -> { "v", "0", "1", "4" }
		
		if (tokens.empty()) continue;
		
		if (tokens[0] == "v")
			vertices.push_back(
				Vector(
					toDouble(tokens[1]),
					toDouble(tokens[2]),
					toDouble(tokens[3])));
		
		if (tokens[0] == "vn")
			normals.push_back(
				Vector(
					toDouble(tokens[1]),
					toDouble(tokens[2]),
					toDouble(tokens[3])));

		if (tokens[0] == "vt")
			uvs.push_back(
				Vector(
					toDouble(tokens[1]),
					toDouble(tokens[2]),
					0));
		
		if (tokens[0] == "f") {
			for (int i = 0; i < int(tokens.size()) - 3; i++) {
				triangles.push_back(
					parseTriangle(tokens[1], tokens[2 + i], tokens[3 + i])
				);
			}
		}
	}
	
	fclose(f);
	
	for (auto& t: triangles) {
		Vector A = vertices[t.v[0]];
		Vector B = vertices[t.v[1]];
		Vector C = vertices[t.v[2]];
		Vector AB = B - A;
		Vector AC = C - A;
		t.gnormal = AB ^ AC;
		t.gnormal.normalize();
		
		// (1, 0) = px * texAB + qx * texAC; (1)
		// (0, 1) = py * texAB + qy * texAC; (2)
		
		Vector texA = uvs[t.t[0]];
		Vector texB = uvs[t.t[1]];
		Vector texC = uvs[t.t[2]];
		
		Vector texAB = texB - texA;
		Vector texAC = texC - texA;
		
		double px, py, qx, qy;
		solve2D(texAB, texAC, Vector(1, 0, 0), px, qx); // (1)
		solve2D(texAB, texAC, Vector(0, 1, 0), py, qy); // (2)
		
		t.dNdx = px * AB + qx * AC;
		t.dNdy = py * AB + qy * AC;
		t.dNdx.normalize();
		t.dNdy.normalize();
	}
	printf("Mesh loaded, %d triangles\n", int(triangles.size()));

	return true;
}