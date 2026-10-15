"""Vectorised UE FTransform / FQuat math (numpy). Quats are (x, y, z, w); UE composition A*B = apply A then B."""
import numpy as np


def qnorm(q):
    return q / np.linalg.norm(q, axis=-1, keepdims=True)


def qmul(a, b):
    """Hamilton product a*b (rotation b then a), matching FQuat operator*."""
    ax, ay, az, aw = np.moveaxis(a, -1, 0)
    bx, by, bz, bw = np.moveaxis(b, -1, 0)
    return np.stack([
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ], axis=-1)


def qconj(q):
    return q * np.array([-1.0, -1.0, -1.0, 1.0])


def qrot(q, v):
    """Rotate vector v by quaternion q."""
    u = q[..., :3]
    w = q[..., 3:4]
    t = 2.0 * np.cross(u, v)
    return v + w * t + np.cross(u, t)


def qaxis(q, axis):
    e = np.zeros(q.shape[:-1] + (3,))
    e[..., axis] = 1.0
    return qrot(q, e)


def nlerp(a, b, t):
    """UE FQuat::FastLerp + Normalize (shortest path)."""
    t = np.asarray(t)[..., None]
    dot = np.sum(a * b, axis=-1, keepdims=True)
    bias = np.where(dot >= 0.0, 1.0, -1.0)
    return qnorm(a * (1.0 - t) + b * (bias * t))


class Xf:
    """Batch of transforms (rotation q[...,4], translation t[...,3]); scale assumed 1."""

    def __init__(self, q, t):
        self.q = np.asarray(q, dtype=np.float64)
        self.t = np.asarray(t, dtype=np.float64)

    @staticmethod
    def identity(shape=()):
        q = np.zeros(shape + (4,)); q[..., 3] = 1.0
        return Xf(q, np.zeros(shape + (3,)))

    def __mul__(self, other):
        """UE A*B: apply self (A) then other (B)."""
        return Xf(qmul(other.q, self.q), qrot(other.q, self.t) + other.t)

    def inverse(self):
        iq = qconj(self.q)
        return Xf(iq, -qrot(iq, self.t))

    def relative_to(self, other):
        """UE A.GetRelativeTransform(B) = A * B^-1."""
        return self * other.inverse()

    def inv_transform_position(self, p):
        return qrot(qconj(self.q), p - self.t)

    def inv_transform_vector(self, v):
        return qrot(qconj(self.q), v)

    def inv_transform_rotation(self, q):
        return qmul(qconj(self.q), q)

    def blend_from_identity(self, alpha):
        """FTransform::Blend(Identity, self, alpha)."""
        ident = Xf.identity(self.q.shape[:-1])
        a = np.asarray(alpha)
        return Xf(nlerp(ident.q, self.q, a), self.t * a[..., None])

    def __getitem__(self, idx):
        return Xf(self.q[idx], self.t[idx])

    @staticmethod
    def where(mask, a, b):
        m = np.asarray(mask)
        return Xf(np.where(m[..., None], a.q, b.q), np.where(m[..., None], a.t, b.t))
