import numpy as np
import scipy as sp
from ..utils.snr import local_correlations_fft
from ..utils.noise_estimator import get_noise_fft

def noise_estimator(Y,range_ff=[0.25,0.5],method='logmexp'):
    dims = Y.shape
    if len(dims)>2:
        V_hat = Y.reshape((np.prod(dims[:2]),dims[2]),order='F')
    else:
        V_hat = Y.copy()
    sns = []
    for i in range(V_hat.shape[0]):
        ff, Pxx = sp.signal.welch(V_hat[i,:],nperseg=min(256,dims[-1]))
        ind1 = ff > range_ff[0]
        ind2 = ff < range_ff[1]
        ind = np.logical_and(ind1, ind2)
        Pxx_ind = Pxx[ind]
        sn = {
            'mean': lambda Pxx_ind: np.sqrt(np.mean(np.divide(Pxx_ind, 2))),
            'median': lambda Pxx_ind: np.sqrt(np.median(np.divide(Pxx_ind, 2))),
            'logmexp': lambda Pxx_ind: np.sqrt(np.exp(np.mean(np.log(np.divide(Pxx_ind, 2)))))
        }[method](Pxx_ind)
        sns.append(sn)
    sns = np.asarray(sns)
    if len(dims)>2:
        sns = sns.reshape(dims[:2],order='F')
    return sns

def covariance_matrix(Y):
    """
    Same as np.cov
    For a tutorial visit: https://people.duke.edu/~ccc14/sta-663/LinearAlgebraReview.html
    # Covariance matrix: outer product of the normalized
    # matrix where every variable has zero mean
    # divided by the number of degrees of freedom
    """
    num_rvs , num_obs = Y.shape
    w = Y - Y.mean(1)[:, np.newaxis]
    Cy = w.dot(w.T)/(num_obs - 1)
    return Cy


def spatial_filter_image(Y_new, gHalf=[2,2], sn=None):
    """
    Apply a wiener filter to image Y_new d1 x d2 x T
    """
    mean_ = Y_new.mean(axis=2,keepdims=True)
    if sn is None:
        sn = noise_estimator(Y_new - mean_, method='logmexp')
    else:
        print('sn given')
    # Cnb = cm.local_correlations(Y_new)
    Cnb = local_correlations_fft(Y_new)
    maps = [Cnb.min(), Cnb.max()]

    Y_new2 = Y_new.copy()
    Y_new3 = np.zeros(Y_new.shape)#Y_new.copy()

    d = np.shape(Y_new)
    n_pixels = np.prod(d[:-1])

    center = np.zeros((n_pixels,2)) #2D arrays

    k_hats=[]
    for pixel in np.arange(n_pixels):
        if pixel % 1e3==0:
            print('first k pixels %d'%pixel)
        ij = np.unravel_index(pixel,d[:2])
        for c, i in enumerate(ij):
            center[pixel, c] = i
        # Get surrounding area
        ijSig = [[np.maximum(ij[c] - gHalf[c], 0), np.minimum(ij[c] + gHalf[c] + 1, d[c])]
                for c in range(len(ij))]

        Y_curr = np.array(Y_new[[slice(*a) for a in ijSig]].copy(),dtype=np.float32)
        sn_curr = np.array(sn[[slice(*a) for a in ijSig]].copy(),dtype=np.float32)
        cc1 = ij[0]-ijSig[0][0]
        cc2 = ij[1]-ijSig[1][0]
        neuron_indx = int(np.ravel_multi_index((cc1,cc2),Y_curr.shape[:2],order='F'))
        Y_out , k_hat = spatial_filter_block(Y_curr, sn=sn_curr,
                maps=maps, neuron_indx=neuron_indx)
        Y_new3[ij[0],ij[1],:] = Y_out[cc1,cc2,:]
        k_hats.append(k_hat)

    return Y_new3, k_hats


def spatial_filter_block(data,sn=None,maps=None,neuron_indx=None):
    """
    Apply wiener filter to block in data d1 x d2 x T
    """
    data = np.asarray(data)
    dims = data.shape
    mean_ = data.mean(2,keepdims=True)
    data_ = data - mean_

    if sn is None:
        # sn, _ = cm.source_extraction.cnmf.pre_processing.get_noise_fft(data_,noise_method='mean')
        sn, _ = get_noise_fft(data_,noise_method='mean')

    sn = sn.reshape(np.prod(dims[:2]),order='F')
    D = np.diag(sn**2)
    data_r = data_.reshape((np.prod(dims[:2]),dims[2]),order='F')
    Cy = covariance_matrix(data_r)
    Cy = Cy.copy()
    try:
        if neuron_indx is None:
            hat_k = np.linalg.inv(Cy).dot(Cy-D)
        else:
            hat_k = np.linalg.inv(Cy).dot(Cy[neuron_indx,:]-D[neuron_indx,:])
    except np.linalg.linalg.LinAlgError as err:
        print('Singular matrix--')
        return data , []
    if neuron_indx is None:
        y_ = hat_k.dot(data_r)
    else:
        y_ = data_r.copy()
        y_[neuron_indx,:] = hat_k[:,np.newaxis].T.dot(data_r)
    y_hat = y_.reshape(dims[:2]+(dims[2],),order='F')
    y_hat = y_hat + mean_
    Cn_y = local_correlations_fft(data)
    Cn_yh = local_correlations_fft(y_hat)
    return y_hat , hat_k
