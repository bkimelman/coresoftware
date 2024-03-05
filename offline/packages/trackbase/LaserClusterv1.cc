/**
 * @file trackbase/LaserClusterv1.cc
 * @author Ben Kimelman
 * @date February 2024
 * @brief Implementation of LaserClusterv1
 */
#include "LaserClusterv1.h"

#include <cmath>
#include <utility>          // for swap

void LaserClusterv1::identify(std::ostream& os) const
{
  os << "---LaserClusterv1--------------------" << std::endl;

  os << " (x,y,z) =  (" << m_pos[0];
  os << ", " << m_pos[1] << ", ";
  os << m_pos[2] << ") cm";

  os << " adc = " << getAdc() << std::endl;

  os << std::endl;
  os << "-----------------------------------------------" << std::endl;

  return;
}

int LaserClusterv1::isValid() const
{
  if(std::isnan(getX())) return 0;
  if(std::isnan(getY())) return 0;
  if(std::isnan(getZ())) return 0;

  if (m_adc == 0xFFFFFFFF) return 0;

  return 1;
}

void LaserClusterv1::CopyFrom( const LaserCluster& source )
{
  // do nothing if copying onto oneself
  if( this == &source ) return;
 
  // parent class method
  LaserCluster::CopyFrom( source );

  setX( source.getX() );
  setY( source.getY() );
  setZ( source.getZ() );
  setAdc( source.getAdc() );

}
