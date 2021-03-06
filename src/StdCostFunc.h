// This file is part of PG.
//
// PG is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// PG is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with PG.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

// include
#include <boost/math/constants/constants.hpp>
// PG
#include "AutoDiffFunction.h"
#include "ConfigStruct.h"
#include "PGData.h"

namespace pg
{

template<typename Type>
class StdCostFunc : public AutoDiffFunction<Type, 1>
{
public:
  typedef AutoDiffFunction<Type, 1> parent_t;
  typedef typename parent_t::scalar_t scalar_t;
  typedef typename parent_t::result_ad_t result_ad_t;
  typedef typename parent_t::argument_t argument_t;

public:
  StdCostFunc(PGData<Type>* pgdata, std::vector<std::vector<double>> q,
              double postureScale, double torqueScale, double forceScale, 
              double ellipseScale,
              const std::vector<BodyPositionTarget>& bodyPosTargets,
              const std::vector<BodyOrientationTarget>& bodyOriTargets,
              const std::vector<ForceContact>& forceContacts,
              const std::vector<ForceContactMinimization>& forceContactsMin)
    : parent_t(pgdata, pgdata->pbSize(), 1, "StdCostFunc")
    , pgdata_(pgdata)
    , tq_(std::move(q))
    , postureScale_(postureScale)
    , torqueScale_(torqueScale)
    , forceScale_(forceScale)
    , ellipseScale_(ellipseScale)
    , bodyPosTargets_(bodyPosTargets.size())
    , bodyOriTargets_(bodyOriTargets.size())
    , forceContactsMin_()
  {
    for(std::size_t i = 0; i < bodyPosTargets_.size(); ++i)
    {
      bodyPosTargets_[i] = {pgdata->multibody().bodyIndexById(bodyPosTargets[i].bodyId),
                            bodyPosTargets[i].target.cast<scalar_t>(),
                            bodyPosTargets[i].scale};
    }

    for(std::size_t i = 0; i < bodyOriTargets_.size(); ++i)
    {
      bodyOriTargets_[i] = {pgdata->multibody().bodyIndexById(bodyOriTargets[i].bodyId),
                            bodyOriTargets[i].target.cast<scalar_t>(),
                            bodyOriTargets[i].scale};
    }

    for(std::size_t i = 0; i < forceContactsMin.size(); ++i)
    {
      for(std::size_t j = 0; j < forceContacts.size(); ++j)
      {
        // we don't break scine it could be many contact on the same body
        if(forceContactsMin[i].bodyId == forceContacts[j].bodyId)
        {
          forceContactsMin_.push_back({j, forceContactsMin[i].scale});
        }
      }
    }
  }


  void impl_compute(result_ad_t& res, const argument_t& /* x */) const throw()
  {
    // compute posture task
    scalar_t posture = scalar_t(0., Eigen::VectorXd::Zero(this->inputSize()));
    if(postureScale_ > 0.)
    {
      const std::vector<std::vector<scalar_t>>& q = pgdata_->q();
      for(int i = 0; i < pgdata_->multibody().nrJoints(); ++i)
      {
        if(pgdata_->multibody().joint(i).params() == 1)
        {
          posture += std::pow(tq_[i][0] - q[i][0], 2);
        }
      }
    }

    // compute torque task
    scalar_t torque = scalar_t(0., Eigen::VectorXd::Zero(this->inputSize()));
    if(torqueScale_ > 0.)
    {
      const auto& torqueVec = pgdata_->id().torque();
      for(int i = 0; i < pgdata_->multibody().nrJoints(); ++i)
      {
        for(int j = 0; j < pgdata_->multibody().joint(i).dof(); ++j)
        {
          torque += std::pow(torqueVec[i](j), 2);
        }
      }
    }

    // compute force task
    scalar_t force(0., Eigen::VectorXd::Zero(this->inputSize()));
    Eigen::Vector3<scalar_t> forceTmp(force, force, force);
    if(forceScale_ > 0.)
    {
      for(const auto& fd: pgdata_->forceDatas())
      {
        forceTmp.setZero();
        for(const sva::ForceVec<scalar_t>& fv: fd.forces)
        {
          forceTmp += fv.force();
        }
        force += forceTmp.squaredNorm()*forceScale_;
      }
    }

    const FK<scalar_t>& fk = pgdata_->fk();
    scalar_t pos = scalar_t(0., Eigen::VectorXd::Zero(this->inputSize()));
    for(const BodyPositionTargetData& bp: bodyPosTargets_)
    {
      pos += (fk.bodyPosW()[bp.bodyIndex].translation() - bp.target).squaredNorm()*
          bp.scale;
    }

    scalar_t ori = scalar_t(0., Eigen::VectorXd::Zero(this->inputSize()));
    for(const BodyOrientationTargetData& bo: bodyOriTargets_)
    {
      const auto& pos = fk.bodyPosW()[bo.bodyIndex];
      ori += std::pow(pos.rotation().row(0).dot(bo.target.row(1)), 2)*bo.scale;
      ori += std::pow(pos.rotation().row(1).dot(bo.target.row(2)), 2)*bo.scale;
      ori += std::pow(pos.rotation().row(2).dot(bo.target.row(0)), 2)*bo.scale;
    }

    scalar_t forceMin = scalar_t(0., Eigen::VectorXd::Zero(this->inputSize()));
    for(const ForceContactMinimizationData& fcmd: forceContactsMin_)
    {
      const auto& forceData = pgdata_->forceDatas()[fcmd.forcePos];
      forceTmp.setZero();
      for(const sva::ForceVec<scalar_t>& fv: forceData.forces)
      {
        forceTmp += fv.force();
      }
      forceMin += forceTmp.squaredNorm()*fcmd.scale;
    }

    //Compute ellipse contact cost
    scalar_t ellipses = scalar_t(0., Eigen::VectorXd::Zero(this->inputSize()));
    if(ellipseScale_ > 0.)
    {
      for(std::size_t i = 0; i < pgdata_->ellipseDatas().size(); ++i)
      {
        ellipses += -boost::math::constants::pi<double>()*pgdata_->ellipseDatas()[i].r1 * pgdata_->ellipseDatas()[i].r2; 
        std::cout << "ellipses cost: " << ellipses << std::endl;
      } 
    }

    res(0) = posture*postureScale_ + torque*torqueScale_ + force +
        pos + ori + forceMin + ellipses*ellipseScale_;
  }

private:
  struct BodyPositionTargetData
  {
    int bodyIndex;
    Eigen::Vector3<scalar_t> target;
    double scale;
  };

  struct BodyOrientationTargetData
  {
    int bodyIndex;
    Eigen::Matrix3<scalar_t> target;
    double scale;
  };

  struct ForceContactMinimizationData
  {
    std::size_t forcePos;
    double scale;
  };

private:
  PGData<Type>* pgdata_;
  std::vector<std::vector<double>> tq_;
  double postureScale_;
  double torqueScale_;
  double forceScale_;
  double ellipseScale_;
  std::vector<BodyPositionTargetData> bodyPosTargets_;
  std::vector<BodyOrientationTargetData> bodyOriTargets_;
  std::vector<ForceContactMinimizationData> forceContactsMin_;
};

} // namespace pg
