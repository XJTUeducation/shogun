/*
 * This software is distributed under BSD 3-clause license (see LICENSE file).
 *
 * Authors: Shell Hu, Abinash Panda, Viktor Gal, Bjoern Esser, Sergey Lisitsyn, 
 *          Jiaolong Xu, Sanuj Sharma
 */

#include <shogun/structure/FactorGraphModel.h>
#include <shogun/structure/Factor.h>
#include <shogun/features/FactorGraphFeatures.h>
#include <shogun/mathematics/Math.h>
#include <shogun/mathematics/linalg/LinalgNamespace.h>

#include <unordered_map>
typedef std::unordered_map<int32_t, int32_t> factor_counts_type;

using namespace shogun;

CFactorGraphModel::CFactorGraphModel()
	: CStructuredModel()
{
	init();
}

CFactorGraphModel::CFactorGraphModel(CFeatures* features, CStructuredLabels* labels,
	EMAPInferType inf_type, bool verbose) : CStructuredModel(features, labels)
{
	init();
	m_inf_type = inf_type;
	m_verbose = verbose;
}

CFactorGraphModel::~CFactorGraphModel()
{
	SG_UNREF(m_factor_types);
}

void CFactorGraphModel::init()
{
	SG_ADD((CSGObject**)&m_factor_types, "factor_types", "Array of factor types", MS_NOT_AVAILABLE);
	SG_ADD(&m_w_cache, "w_cache", "Cache of global parameters", MS_NOT_AVAILABLE);
	SG_ADD(&m_w_map, "w_map", "Parameter mapping", MS_NOT_AVAILABLE);

	m_inf_type = TREE_MAX_PROD;
	m_factor_types = new CDynamicObjectArray();
	m_verbose = false;

	SG_REF(m_factor_types);
}

void CFactorGraphModel::add_factor_type(CFactorType* ftype)
{
	REQUIRE(ftype->get_w_dim() > 0, "%s::add_factor_type(): number of parameters can't be 0!\n",
		get_name());

	// check whether this ftype has been added
	int32_t id = ftype->get_type_id();
	for (int32_t fi = 0; fi < m_factor_types->get_num_elements(); ++fi)
	{
		CFactorType* ft= dynamic_cast<CFactorType*>(m_factor_types->get_element(fi));
		if (id == ft->get_type_id())
		{
			SG_UNREF(ft);
			SG_PRINT("%s::add_factor_type(): factor_type (id = %d) has been added!\n",
				get_name(), id);

			return;
		}

		SG_UNREF(ft);
	}

	SGVector<int32_t> w_map_cp = m_w_map.clone();
	m_w_map.resize_vector(w_map_cp.size() + ftype->get_w_dim());

	for (int32_t mi = 0; mi < w_map_cp.size(); mi++)
	{
		m_w_map[mi] = w_map_cp[mi];
	}
	// add new mapping in the end
	for (int32_t mi = w_map_cp.size(); mi < m_w_map.size(); mi++)
	{
		m_w_map[mi] = id;
	}

	// push factor type
	m_factor_types->push_back(ftype);

	// initialize w cache
	fparams_to_w();

	if (m_verbose)
	{
		m_w_map.display_vector("add_factor_type(): m_w_map");
	}
}

void CFactorGraphModel::del_factor_type(const int32_t ftype_id)
{
	int w_dim = 0;
	// delete from m_factor_types
	for (int32_t fi = 0; fi < m_factor_types->get_num_elements(); ++fi)
	{
		CFactorType* ftype = dynamic_cast<CFactorType*>(m_factor_types->get_element(fi));
		if (ftype_id == ftype->get_type_id())
		{
			w_dim = ftype->get_w_dim();
			SG_UNREF(ftype);
			m_factor_types->delete_element(fi);
			break;
		}

		SG_UNREF(ftype);
	}

	ASSERT(w_dim != 0);

	SGVector<int32_t> w_map_cp = m_w_map.clone();
	m_w_map.resize_vector(w_map_cp.size() - w_dim);

	int ind = 0;
	for (int32_t mi = 0; mi < w_map_cp.size(); mi++)
	{
		if (w_map_cp[mi] == ftype_id)
			continue;

		m_w_map[ind++] = w_map_cp[mi];
	}

	ASSERT(ind == m_w_map.size());
}

CDynamicObjectArray* CFactorGraphModel::get_factor_types() const
{
	SG_REF(m_factor_types);
	return m_factor_types;
}

CFactorType* CFactorGraphModel::get_factor_type(const int32_t ftype_id) const
{
	for (int32_t fi = 0; fi < m_factor_types->get_num_elements(); ++fi)
	{
		CFactorType* ftype = dynamic_cast<CFactorType*>(m_factor_types->get_element(fi));
		if (ftype_id == ftype->get_type_id())
			return ftype;

		SG_UNREF(ftype);
	}

	return NULL;
}

SGVector<int32_t> CFactorGraphModel::get_global_params_mapping() const
{
	return m_w_map.clone();
}

SGVector<int32_t> CFactorGraphModel::get_params_mapping(const int32_t ftype_id)
{
	return m_w_map.find(ftype_id);
}

int32_t CFactorGraphModel::get_dim() const
{
	return m_w_map.size();
}

SGVector<float64_t> CFactorGraphModel::fparams_to_w()
{
	REQUIRE(m_factor_types != NULL, "%s::fparams_to_w(): no factor types!\n", get_name());

	if (m_w_cache.size() != get_dim())
		m_w_cache.resize_vector(get_dim());

	int32_t offset = 0;
	for (int32_t fi = 0; fi < m_factor_types->get_num_elements(); ++fi)
	{
		CFactorType* ftype = dynamic_cast<CFactorType*>(m_factor_types->get_element(fi));
		int32_t w_dim = ftype->get_w_dim();
		offset += w_dim;
		SGVector<float64_t> fw = ftype->get_w();
		SGVector<int32_t> fw_map = get_params_mapping(ftype->get_type_id());

		ASSERT(fw_map.size() == fw.size());

		for (int32_t wi = 0; wi < w_dim; wi++)
			m_w_cache[fw_map[wi]] = fw[wi];

		SG_UNREF(ftype);
	}

	ASSERT(offset == m_w_cache.size());

	return m_w_cache;
}

void CFactorGraphModel::w_to_fparams(SGVector<float64_t> w)
{
	// if nothing changed
	if (m_w_cache.equals(w))
		return;

	if (m_verbose)
		SG_SPRINT("****** update m_w_cache!\n");

	ASSERT(w.size() == m_w_cache.size());
	m_w_cache = w.clone();

	int32_t offset = 0;
	for (int32_t fi = 0; fi < m_factor_types->get_num_elements(); ++fi)
	{
		CFactorType* ftype = dynamic_cast<CFactorType*>(m_factor_types->get_element(fi));
		int32_t w_dim = ftype->get_w_dim();
		offset += w_dim;
		SGVector<float64_t> fw(w_dim);
		SGVector<int32_t> fw_map = get_params_mapping(ftype->get_type_id());

		for (int32_t wi = 0; wi < w_dim; wi++)
			fw[wi] = m_w_cache[fw_map[wi]];

		ftype->set_w(fw);
		SG_UNREF(ftype);
	}

	ASSERT(offset == m_w_cache.size());
}

SGVector< float64_t > CFactorGraphModel::get_joint_feature_vector(int32_t feat_idx, CStructuredData* y)
{
	// factor graph instance
	CFactorGraphFeatures* mf = m_features->as<CFactorGraphFeatures>();
	CFactorGraph* fg = mf->get_sample(feat_idx);

	// ground truth states
	CFactorGraphObservation* fg_states = y->as<CFactorGraphObservation>();
	SGVector<int32_t> states = fg_states->get_data();

	// initialize psi
	SGVector<float64_t> psi(get_dim());
	psi.zero();

	// construct unnormalized psi
	CDynamicObjectArray* facs = fg->get_factors();
	for (int32_t fi = 0; fi < facs->get_num_elements(); ++fi)
	{
		CFactor* fac = dynamic_cast<CFactor*>(facs->get_element(fi));
		CTableFactorType* ftype = fac->get_factor_type();
		int32_t id = ftype->get_type_id();
		SGVector<int32_t> w_map = get_params_mapping(id);

		ASSERT(w_map.size() == ftype->get_w_dim());

		SGVector<float64_t> dat = fac->get_data();
		int32_t dat_size = dat.size();

		ASSERT(w_map.size() == dat_size * ftype->get_num_assignments());

		int32_t ei = ftype->index_from_universe_assignment(states, fac->get_variables());
		for (int32_t di = 0; di < dat_size; di++)
			psi[w_map[ei*dat_size + di]] += dat[di];

		SG_UNREF(ftype);
		SG_UNREF(fac);
	}

	// negation (-E(x,y) = <w,phi(x,y)>)
	psi.scale(-1.0);

	SG_UNREF(facs);
	SG_UNREF(fg);

	return psi;
}

// E(x_i, y; w) - E(x_i, y_i; w) >= L(y_i, y) - xi_i
// xi_i >= max oracle
// max oracle := argmax_y { L(y_i, y) - E(x_i, y; w) + E(x_i, y_i; w) }
//            := argmin_y { -L(y_i, y) + E(x_i, y; w) } - E(x_i, y_i; w)
// we do energy minimization in inference, so get back to max oracle value is:
// [ L(y_i, y_star) - E(x_i, y_star; w) ] + E(x_i, y_i; w)
CResultSet* CFactorGraphModel::argmax(SGVector<float64_t> w, int32_t feat_idx, bool const training)
{
	// factor graph instance
	CFactorGraphFeatures* mf = m_features->as<CFactorGraphFeatures>();
	CFactorGraph* fg = mf->get_sample(feat_idx);

	// prepare factor graph
	fg->connect_components();
	if (m_inf_type == TREE_MAX_PROD)
	{
		ASSERT(fg->is_tree_graph());
	}

	if (m_verbose)
		SG_SPRINT("\n------ example %d\n", feat_idx);

	// update factor parameters
	w_to_fparams(w);
	fg->compute_energies();

	if (m_verbose)
	{
		SG_SPRINT("energy table before loss-aug:\n");
		fg->evaluate_energies();
	}

	// prepare CResultSet
	CResultSet* ret = new CResultSet();
	SG_REF(ret);
	ret->psi_computed = true;

	// y_truth
	CFactorGraphObservation* y_truth = m_labels->get_label(feat_idx)->as<CFactorGraphObservation>();

	SGVector<int32_t> states_gt = y_truth->get_data();

	// E(x_i, y_i; w)
	ret->psi_truth = get_joint_feature_vector(feat_idx, y_truth);
	float64_t energy_gt = fg->evaluate_energy(states_gt);
	ret->score = energy_gt;

	// - min_y [ E(x_i, y; w) - delta(y_i, y) ]
	if (training)
	{
		fg->loss_augmentation(y_truth); // wrong assignments -delta()

		if (m_verbose)
		{
			SG_SPRINT("energy table after loss-aug:\n");
			fg->evaluate_energies();
		}
	}

	CMAPInference infer_met(fg, m_inf_type);
	infer_met.inference();

	// y_star
	CFactorGraphObservation* y_star = infer_met.get_structured_outputs();
	SGVector<int32_t> states_star = y_star->get_data();

	ret->argmax = y_star;
	ret->psi_pred = get_joint_feature_vector(feat_idx, y_star);
	float64_t l_energy_pred = fg->evaluate_energy(states_star);
	ret->score -= l_energy_pred;
	ret->delta = delta_loss(y_truth, y_star);

	if (m_verbose)
	{
		float64_t dot_pred = linalg::dot(w, ret->psi_pred);
		float64_t dot_truth = linalg::dot(w, ret->psi_truth);
		float64_t slack =  dot_pred + ret->delta - dot_truth;

		SG_SPRINT("\n");
		w.display_vector("w");

		ret->psi_pred.display_vector("psi_pred");
		states_star.display_vector("state_pred");

		SG_SPRINT("dot_pred = %f, energy_pred = %f, delta = %f\n\n", dot_pred, l_energy_pred, ret->delta);

		ret->psi_truth.display_vector("psi_truth");
		states_gt.display_vector("state_gt");

		SG_SPRINT("dot_truth = %f, energy_gt = %f\n\n", dot_truth, energy_gt);

		SG_SPRINT("slack = %f, score = %f\n\n", slack, ret->score);
	}

	SG_UNREF(y_truth);
	SG_UNREF(fg);

	return ret;
}

float64_t CFactorGraphModel::delta_loss(CStructuredData* y1, CStructuredData* y2)
{
	CFactorGraphObservation* y_truth = y1->as<CFactorGraphObservation>();
	CFactorGraphObservation* y_pred = y2->as<CFactorGraphObservation>();
	SGVector<int32_t> s_truth = y_truth->get_data();
	SGVector<int32_t> s_pred = y_pred->get_data();

	ASSERT(s_pred.size() == s_truth.size());

	float64_t loss = 0.0;
	for (int32_t si = 0; si < s_pred.size(); si++)
	{
		if (s_pred[si] != s_truth[si])
			loss += y_truth->get_loss_weights()[si];
	}

	return loss;
}

void CFactorGraphModel::init_training()
{
}

void CFactorGraphModel::init_primal_opt(
		float64_t regularization,
		SGMatrix< float64_t > & A,
		SGVector< float64_t > a,
		SGMatrix< float64_t > B,
		SGVector< float64_t > & b,
		SGVector< float64_t > & lb,
		SGVector< float64_t > & ub,
		SGMatrix< float64_t > & C)
{
	C = SGMatrix< float64_t >::create_identity_matrix(get_dim(), regularization);
	REQUIRE(m_factor_types != NULL, "%s::init_primal_opt(): no factor types!\n", get_name());

	int32_t dim_w = get_dim();

	switch (m_inf_type)
	{
		case GRAPH_CUT:
			lb.resize_vector(dim_w);
			ub.resize_vector(dim_w);
			SGVector< float64_t >::fill_vector(lb.vector, lb.vlen, -CMath::INFTY);
			SGVector< float64_t >::fill_vector(ub.vector, ub.vlen, CMath::INFTY);

			for (int32_t fi = 0; fi < m_factor_types->get_num_elements(); ++fi)
			{
				CFactorType* ftype = dynamic_cast<CFactorType*>(m_factor_types->get_element(fi));
				int32_t w_dim = ftype->get_w_dim();
				SGVector<int32_t> card = ftype->get_cardinalities();

				// TODO: Features of pairwise factor are assume to be 1. Consider more general case, e.g., edge features are availabel.
				// for pairwise factors with binary labels
				if (card.size() == 2 &&  card[0] == 2 && card[1] == 2)
				{
					REQUIRE(w_dim == 4, "GraphCut doesn't support edge features currently.");
					SGVector<float64_t> fw = ftype->get_w();
					SGVector<int32_t> fw_map = get_params_mapping(ftype->get_type_id());
					ASSERT(fw_map.size() == fw.size());

					// submodularity constrain
					// E(0,1) + E(1,0) - E(0,0) + E(1,1) > 0
					// For pairwise factors, data term = 1,
					// energy table indeces are defined as follows:
					// w[0]*1 = E(0, 0)
					// w[1]*1 = E(1, 0)
					// w[2]*1 = E(0, 1)
					// w[3]*1 = E(1, 1)
					// thus, w[2] + w[1] - w[0] - w[3] > 0
					// since factor graph model is over-parametering,
					// the constrain can be written as w[2] > 0, w[1] > 0, w[0] = 0, w[3] = 0
					lb[fw_map[0]] = 0;
					ub[fw_map[0]] = 0;
					lb[fw_map[3]] = 0;
					ub[fw_map[3]] = 0;
					lb[fw_map[1]] = 0;
					lb[fw_map[2]] = 0;
				}
				SG_UNREF(ftype);
			}
			break;
		case TREE_MAX_PROD:
		case LOOPY_MAX_PROD:
		case LP_RELAXATION:
		case TRWS_MAX_PROD:
		case GEMPLP:
			break;
	}
}
