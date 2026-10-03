import Link from "next/link";

import { dogAppPath } from "../../lib/auth/protected-route";
import type { DogSummaryDto } from "../../lib/data-access/dogs";

const ROLE_LABELS = {
  owner: "PROPIETARIO",
  editor: "EDITOR",
  viewer: "LECTOR",
} as const satisfies Record<DogSummaryDto["role"], string>;

export function MembershipSelector({
  dogs,
}: Readonly<{ dogs: readonly DogSummaryDto[] }>) {
  return (
    <nav aria-label="Perros disponibles">
      <ul>
        {dogs.map((dog) => (
          <li key={dog.id}>
            <Link className="button-link" href={dogAppPath(dog.id, "today")}>
              {dog.name} · {ROLE_LABELS[dog.role]}
            </Link>
          </li>
        ))}
      </ul>
    </nav>
  );
}
